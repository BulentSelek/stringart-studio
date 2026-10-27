#pragma once

#include "core/GrayImage.h"
#include "core/PinGeometry.h"
#include "core/PlaybackRenderer.h"
#include "core/ProjectFile.h"
#include "core/ThreadRenderer.h"

#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QtQmlIntegration>

#include <memory>
#include <optional>
#include <vector>

class ImageStore;
class QSettings;
class OptimizationWorker;

// UI state and commands for the QML front end. Owns the original image, the
// cached preprocessing stages and the last optimization result. All heavy
// work happens on the OptimizationWorker thread or in QtConcurrent tasks.
class ProjectController final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created in C++ and passed to Main.qml")

public:
    enum FrameShape { Circle, Ellipse, Rectangle, Custom };
    Q_ENUM(FrameShape)

private:
    // Frame (pin geometry). Changing it re-derives the canvas size.
    Q_PROPERTY(FrameShape frameShape MEMBER m_frameShape NOTIFY layoutChanged)
    Q_PROPERTY(int pinCount MEMBER m_pinCount NOTIFY layoutChanged)
    Q_PROPERTY(double frameWidthMm MEMBER m_frameWidthMm NOTIFY layoutChanged)
    Q_PROPERTY(double frameHeightMm MEMBER m_frameHeightMm NOTIFY layoutChanged)
    Q_PROPERTY(int workingSize MEMBER m_workingSize NOTIFY layoutChanged)
    Q_PROPERTY(int customPinCount READ customPinCount NOTIFY layoutChanged)
    Q_PROPERTY(int canvasWidth READ canvasWidth NOTIFY layoutChanged)
    Q_PROPERTY(int canvasHeight READ canvasHeight NOTIFY layoutChanged)
    Q_PROPERTY(double mmPerPixel READ mmPerPixel NOTIFY layoutChanged)
    Q_PROPERTY(QVariantList pinPositions READ pinPositions NOTIFY layoutChanged)

    // Optimization settings (snapshotted when a run starts).
    Q_PROPERTY(int maxSegments MEMBER m_maxSegments NOTIFY settingsChanged)
    Q_PROPERTY(int minSeparation MEMBER m_minSeparation NOTIFY settingsChanged)
    Q_PROPERTY(int startPin MEMBER m_startPin NOTIFY settingsChanged)
    Q_PROPERTY(double opacity MEMBER m_opacity NOTIFY settingsChanged)
    Q_PROPERTY(double threadWidth MEMBER m_threadWidth NOTIFY settingsChanged)
    Q_PROPERTY(double maxLengthM MEMBER m_maxLengthM NOTIFY settingsChanged)
    Q_PROPERTY(bool allowRepeats MEMBER m_allowRepeats NOTIFY settingsChanged)
    Q_PROPERTY(int refinePasses MEMBER m_refinePasses NOTIFY settingsChanged)
    Q_PROPERTY(int refineNeighborhood MEMBER m_refineNeighborhood NOTIFY settingsChanged)
    Q_PROPERTY(double edgeEmphasis MEMBER m_edgeEmphasis NOTIFY settingsChanged)
    // Multi-colour mode: palette colours are optimized, not just displayed.
    Q_PROPERTY(bool colorMode MEMBER m_colorMode NOTIFY colorModeChanged)
    Q_PROPERTY(QVariantList palette READ palette NOTIFY paletteChanged)
    Q_PROPERTY(QColor colorBackground MEMBER m_colorBackground NOTIFY paletteChanged)
    Q_PROPERTY(bool resultIsColor READ resultIsColor NOTIFY stateChanged)
    Q_PROPERTY(QColor currentThreadColor READ currentThreadColor NOTIFY playbackChanged)
    Q_PROPERTY(QColor threadColor MEMBER m_threadColor NOTIFY renderStyleChanged)
    Q_PROPERTY(QColor backgroundColor MEMBER m_backgroundColor NOTIFY renderStyleChanged)

    // Preprocessing. Tone applies immediately; crop/rotation are debounced.
    Q_PROPERTY(double brightness MEMBER m_brightness NOTIFY toneChanged)
    Q_PROPERTY(double contrast MEMBER m_contrast NOTIFY toneChanged)
    Q_PROPERTY(double gamma MEMBER m_gamma NOTIFY toneChanged)
    Q_PROPERTY(bool invert MEMBER m_invert NOTIFY toneChanged)
    Q_PROPERTY(double rotation MEMBER m_rotation NOTIFY geometryChanged)
    Q_PROPERTY(QRectF crop MEMBER m_crop NOTIFY geometryChanged)
    Q_PROPERTY(QRectF effectiveCrop READ effectiveCrop NOTIFY effectiveCropChanged)

    // Export settings.
    Q_PROPERTY(int exportSize MEMBER m_exportSize NOTIFY settingsChanged)
    Q_PROPERTY(bool csvMillimeters MEMBER m_csvMillimeters NOTIFY settingsChanged)

    // State.
    Q_PROPERTY(bool hasImage READ hasImage NOTIFY imagesChanged)
    Q_PROPERTY(bool hasResult READ hasResult NOTIFY stateChanged)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY stateChanged)
    Q_PROPERTY(bool exporting READ exporting NOTIFY stateChanged)
    Q_PROPERTY(QString imageName READ imageName NOTIFY imagesChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(int originalRevision READ originalRevision NOTIFY imagesChanged)
    Q_PROPERTY(int processedRevision READ processedRevision NOTIFY imagesChanged)
    Q_PROPERTY(int resultRevision READ resultRevision NOTIFY resultRevisionChanged)
    Q_PROPERTY(QVariantList histogram READ histogram NOTIFY imagesChanged)

    // Progress of the current or last run.
    Q_PROPERTY(int segments READ segments NOTIFY progressChanged)
    Q_PROPERTY(int runMaxSegments READ runMaxSegments NOTIFY progressChanged)
    Q_PROPERTY(double error READ error NOTIFY progressChanged)
    Q_PROPERTY(double initialError READ initialError NOTIFY progressChanged)
    Q_PROPERTY(double elapsedSeconds READ elapsedSeconds NOTIFY progressChanged)
    Q_PROPERTY(double threadLengthM READ threadLengthM NOTIFY progressChanged)

    // Algorithm B: virtual least-squares reconstruction of the result's lines.
    Q_PROPERTY(bool hasVirtual READ hasVirtual NOTIFY virtualChanged)
    Q_PROPERTY(bool computingVirtual READ computingVirtual NOTIFY virtualChanged)
    Q_PROPERTY(double virtualError READ virtualError NOTIFY virtualChanged)
    Q_PROPERTY(int virtualLines READ virtualLines NOTIFY virtualChanged)
    Q_PROPERTY(int virtualRevision READ virtualRevision NOTIFY virtualChanged)

    // Winding playback of the finished sequence (never re-optimizes).
    Q_PROPERTY(int resultSegments READ resultSegments NOTIFY stateChanged)
    Q_PROPERTY(int playbackPosition READ playbackPosition WRITE setPlaybackPosition NOTIFY playbackChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playbackChanged)
    Q_PROPERTY(double playbackSpeed MEMBER m_playbackSpeed NOTIFY playbackSpeedChanged)
    Q_PROPERTY(int currentFromPin READ currentFromPin NOTIFY playbackChanged)
    Q_PROPERTY(int currentToPin READ currentToPin NOTIFY playbackChanged)
    Q_PROPERTY(QVariantList currentSegmentLine READ currentSegmentLine NOTIFY playbackChanged)

public:
    explicit ProjectController(std::shared_ptr<ImageStore> store, QObject *parent = nullptr);
    ~ProjectController() override;

    Q_INVOKABLE bool loadImage(const QUrl &url);
    Q_INVOKABLE bool importPins(const QUrl &url);
    Q_INVOKABLE void rotateBy(double degrees);
    Q_INVOKABLE void resetCrop();
    Q_INVOKABLE void addPaletteColor(const QColor &color);
    Q_INVOKABLE void removePaletteColor(int index);
    Q_INVOKABLE void setPaletteColor(int index, const QColor &color);
    Q_INVOKABLE void setPalette(const QVariantList &colors, const QColor &background);
    Q_INVOKABLE void fitWholeImage();
    Q_INVOKABLE void start();
    // Algorithm C on the current result (continuous thread preserved).
    Q_INVOKABLE void refineSequence();
    // Algorithm B; result shown separately as a non-manufacturable reconstruction.
    Q_INVOKABLE void computeVirtual();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void exportPng(const QUrl &url);
    Q_INVOKABLE void exportCsv(const QUrl &url);
    Q_INVOKABLE void exportSvg(const QUrl &url);
    Q_INVOKABLE void exportJson(const QUrl &url);
    Q_INVOKABLE bool saveProject(const QUrl &url);
    Q_INVOKABLE void play();
    Q_INVOKABLE void stopPlayback();
    Q_INVOKABLE void stepPlayback(int delta);
    Q_INVOKABLE void exportFrame(const QUrl &url);
    Q_INVOKABLE bool openProject(const QUrl &url);

    // Last-used parameters (frame, pins, thread, tone, run and export
    // options). Crop and rotation belong to an image and are not stored.
    void saveSettings(QSettings &settings) const;
    void restoreSettings(QSettings &settings);

    [[nodiscard]] int customPinCount() const { return static_cast<int>(m_customPinsMm.size()); }
    [[nodiscard]] int canvasWidth() const;
    [[nodiscard]] int canvasHeight() const;
    [[nodiscard]] double mmPerPixel() const;
    [[nodiscard]] QVariantList pinPositions() const;
    [[nodiscard]] QRectF effectiveCrop() const;
    [[nodiscard]] bool hasImage() const { return !m_source.isNull(); }
    [[nodiscard]] bool hasResult() const { return m_resultSequence.size() > 1 || !m_resultColorSteps.empty(); }
    [[nodiscard]] bool resultIsColor() const { return !m_resultColorSteps.empty(); }
    [[nodiscard]] QVariantList palette() const;
    [[nodiscard]] QColor currentThreadColor() const;
    [[nodiscard]] bool running() const { return m_running; }
    [[nodiscard]] bool paused() const { return m_paused; }
    [[nodiscard]] bool exporting() const { return m_exporting; }
    [[nodiscard]] QString imageName() const { return m_imageName; }
    [[nodiscard]] QString statusText() const { return m_statusText; }
    [[nodiscard]] int originalRevision() const { return m_originalRevision; }
    [[nodiscard]] int processedRevision() const { return m_processedRevision; }
    [[nodiscard]] int resultRevision() const { return m_resultRevision; }
    [[nodiscard]] QVariantList histogram() const { return m_histogram; }
    [[nodiscard]] int segments() const { return m_segments; }
    [[nodiscard]] int runMaxSegments() const { return m_runMaxSegments; }
    [[nodiscard]] double error() const { return m_error; }
    [[nodiscard]] double initialError() const { return m_initialError; }
    [[nodiscard]] double elapsedSeconds() const { return m_elapsedSeconds; }
    [[nodiscard]] double threadLengthM() const { return m_threadLengthMm / 1000.0; }
    [[nodiscard]] bool hasVirtual() const { return m_virtualLines > 0; }
    [[nodiscard]] bool computingVirtual() const { return m_computingVirtual; }
    [[nodiscard]] double virtualError() const { return m_virtualError; }
    [[nodiscard]] int virtualLines() const { return m_virtualLines; }
    [[nodiscard]] int virtualRevision() const { return m_virtualRevision; }
    [[nodiscard]] int resultSegments() const
    {
        if (resultIsColor())
            return static_cast<int>(m_resultColorSteps.size());
        return m_resultSequence.size() > 1 ? static_cast<int>(m_resultSequence.size()) - 1 : 0;
    }
    [[nodiscard]] int playbackPosition() const { return m_playbackPosition; }
    void setPlaybackPosition(int position);
    [[nodiscard]] bool playing() const { return m_playTimer.isActive(); }
    [[nodiscard]] int currentFromPin() const;
    [[nodiscard]] int currentToPin() const;
    [[nodiscard]] QVariantList currentSegmentLine() const;

signals:
    void layoutChanged();
    void settingsChanged();
    void renderStyleChanged();
    void toneChanged();
    void geometryChanged();
    void effectiveCropChanged();
    void imagesChanged();
    void stateChanged();
    void statusTextChanged();
    void resultRevisionChanged();
    void progressChanged();
    void playbackChanged();
    void virtualChanged();
    void colorModeChanged();
    void paletteChanged();
    void playbackSpeedChanged();

private:
    void setResult(std::vector<int> sequence, std::vector<float> transmittance);
    void setColorResult(std::vector<sa::ColorStep> steps, std::vector<float> rgb);
    void resetResultState();
    void advancePlayback();
    [[nodiscard]] sa::FrameSpec frameSpec() const;
    [[nodiscard]] std::optional<sa::PinLayout> currentLayout() const;
    [[nodiscard]] sa::ProjectData currentSettings() const;
    [[nodiscard]] sa::ProjectData projectForSaving() const;
    void applySettings(const sa::ProjectData &project);
    bool setImage(QImage image, QByteArray bytes, const QString &path);
    void scheduleBaseUpdate();
    void updateBase();
    void updateProcessed();
    void updateResultImages();
    void clearResult();
    void onWorkerProgress();
    void onWorkerFinished(bool success);
    void setStatus(const QString &text);
    void setState(bool running, bool paused);
    [[nodiscard]] sa::RenderStyle renderStyle() const;
    [[nodiscard]] static QString localPath(const QUrl &url);

    std::shared_ptr<ImageStore> m_store;
    OptimizationWorker *m_worker = nullptr;
    QTimer m_baseTimer; // debounces crop, rotation and layout changes

    FrameShape m_frameShape = Circle;
    int m_pinCount = 240;
    double m_frameWidthMm = 500.0;
    double m_frameHeightMm = 400.0; // used by ellipse/rectangle; circle uses the width
    int m_workingSize = 512;
    std::vector<sa::Vec2> m_customPinsMm;

    int m_maxSegments = 4000;
    int m_minSeparation = 20;
    int m_startPin = 0;
    double m_opacity = 0.2;
    double m_threadWidth = 1.0;
    double m_maxLengthM = 0.0;
    bool m_allowRepeats = false;
    int m_refinePasses = 3;
    int m_refineNeighborhood = 20;
    bool m_refining = false;
    double m_refineStartError = 0.0;
    double m_edgeEmphasis = 0.0;
    bool m_colorMode = false;
    std::vector<QColor> m_palette{Qt::black, QColor(0xb0, 0x3a, 0x2e), QColor(0x1f, 0x4e, 0x8c)};
    QColor m_colorBackground = Qt::white;
    QColor m_threadColor = Qt::black;
    QColor m_backgroundColor = Qt::white;
    double m_brightness = 0.0;
    double m_contrast = 1.0;
    double m_gamma = 1.0;
    bool m_invert = false;
    double m_rotation = 0.0;
    QRectF m_crop;
    int m_exportSize = 2048;
    bool m_csvMillimeters = true;

    // m_source is the original limited to kMaxSourceSize on the long side; it
    // feeds both the preview and the working image (working sizes are far
    // smaller), so rotation stays interactive for large photos.
    QImage m_source;
    QSize m_rotatedSize;      // size of m_source after rotation (crop reference)
    QByteArray m_sourceBytes; // original file bytes, embedded in project files
    QString m_imagePath;
    QString m_imageName;
    sa::GrayImage m_base;
    sa::GrayImage m_processed;
    sa::RgbImage m_baseRgb;      // colour mode only
    sa::RgbImage m_processedRgb; // colour mode only
    QVariantList m_histogram;

    bool m_running = false;
    bool m_paused = false;
    bool m_exporting = false;
    QString m_statusText;
    int m_originalRevision = 0;
    int m_processedRevision = 0;
    int m_resultRevision = 0;

    int m_segments = 0;
    int m_runMaxSegments = 0;
    double m_error = 0.0;
    double m_initialError = 0.0;
    double m_elapsedSeconds = 0.0;
    double m_threadLengthMm = 0.0;
    QString m_stopReason;

    // Settings, layout and target of the run that produced m_resultSequence.
    sa::ProjectData m_runSettings;
    std::optional<sa::PinLayout> m_resultLayout;
    std::vector<float> m_resultTarget;
    std::vector<float> m_resultWeights;
    std::vector<int> m_resultSequence;
    std::vector<float> m_resultTransmittance;
    std::vector<sa::ColorStep> m_resultColorSteps;          // multi-colour result
    std::array<std::vector<float>, 3> m_resultColorTarget; // for the colour residual map
    std::unique_ptr<sa::ColorPlaybackRenderer> m_colorPlayback;

    bool m_computingVirtual = false;
    double m_virtualError = 0.0;
    int m_virtualLines = 0;
    int m_virtualRevision = 0;
    int m_resultGeneration = 0; // invalidates virtual results computed for an older result

    std::unique_ptr<sa::PlaybackRenderer> m_playback;
    int m_playbackPosition = 0;
    double m_playbackSpeed = 50.0; // segments per second
    double m_playbackCarry = 0.0;  // fractional segments accumulated between ticks
    QTimer m_playTimer;
    QElapsedTimer m_playClock;
};
