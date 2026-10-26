#pragma once

#include "core/GrayImage.h"
#include "core/PinGeometry.h"
#include "core/ThreadRenderer.h"

#include <QColor>
#include <QImage>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QtQmlIntegration>

#include <memory>
#include <optional>
#include <vector>

class ImageStore;
class OptimizationWorker;

// UI state and commands for the QML front end. Owns the original image, the
// cached preprocessing stages and the last optimization result. All heavy
// work happens on the OptimizationWorker thread or in QtConcurrent tasks.
class ProjectController final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created in C++ and passed to Main.qml")

    // Optimization settings (snapshotted when a run starts).
    Q_PROPERTY(int pinCount MEMBER m_pinCount NOTIFY pinCountChanged)
    Q_PROPERTY(int workingSize MEMBER m_workingSize NOTIFY workingSizeChanged)
    Q_PROPERTY(int maxSegments MEMBER m_maxSegments NOTIFY settingsChanged)
    Q_PROPERTY(int minSeparation MEMBER m_minSeparation NOTIFY settingsChanged)
    Q_PROPERTY(int startPin MEMBER m_startPin NOTIFY settingsChanged)
    Q_PROPERTY(double opacity MEMBER m_opacity NOTIFY settingsChanged)
    Q_PROPERTY(double threadWidth MEMBER m_threadWidth NOTIFY settingsChanged)
    Q_PROPERTY(double diameterMm MEMBER m_diameterMm NOTIFY settingsChanged)
    Q_PROPERTY(double maxLengthM MEMBER m_maxLengthM NOTIFY settingsChanged)
    Q_PROPERTY(bool allowRepeats MEMBER m_allowRepeats NOTIFY settingsChanged)
    Q_PROPERTY(QColor threadColor MEMBER m_threadColor NOTIFY renderStyleChanged)
    Q_PROPERTY(QColor backgroundColor MEMBER m_backgroundColor NOTIFY renderStyleChanged)

    // Preprocessing (applied immediately to the processed preview).
    Q_PROPERTY(double brightness MEMBER m_brightness NOTIFY toneChanged)
    Q_PROPERTY(double contrast MEMBER m_contrast NOTIFY toneChanged)
    Q_PROPERTY(double gamma MEMBER m_gamma NOTIFY toneChanged)
    Q_PROPERTY(bool invert MEMBER m_invert NOTIFY toneChanged)

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
    Q_PROPERTY(QVariantList pinPositions READ pinPositions NOTIFY pinPositionsChanged)

    // Progress of the current or last run.
    Q_PROPERTY(int segments READ segments NOTIFY progressChanged)
    Q_PROPERTY(int runMaxSegments READ runMaxSegments NOTIFY progressChanged)
    Q_PROPERTY(double error READ error NOTIFY progressChanged)
    Q_PROPERTY(double initialError READ initialError NOTIFY progressChanged)
    Q_PROPERTY(double elapsedSeconds READ elapsedSeconds NOTIFY progressChanged)
    Q_PROPERTY(double threadLengthM READ threadLengthM NOTIFY progressChanged)

public:
    explicit ProjectController(std::shared_ptr<ImageStore> store, QObject *parent = nullptr);
    ~ProjectController() override;

    Q_INVOKABLE bool loadImage(const QUrl &url);
    Q_INVOKABLE void start();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void exportPng(const QUrl &url);
    Q_INVOKABLE void exportCsv(const QUrl &url);

    [[nodiscard]] bool hasImage() const { return !m_original.isNull(); }
    [[nodiscard]] bool hasResult() const { return m_resultSequence.size() > 1; }
    [[nodiscard]] bool running() const { return m_running; }
    [[nodiscard]] bool paused() const { return m_paused; }
    [[nodiscard]] bool exporting() const { return m_exporting; }
    [[nodiscard]] QString imageName() const { return m_imageName; }
    [[nodiscard]] QString statusText() const { return m_statusText; }
    [[nodiscard]] int originalRevision() const { return m_originalRevision; }
    [[nodiscard]] int processedRevision() const { return m_processedRevision; }
    [[nodiscard]] int resultRevision() const { return m_resultRevision; }
    [[nodiscard]] QVariantList histogram() const { return m_histogram; }
    [[nodiscard]] QVariantList pinPositions() const;
    [[nodiscard]] int segments() const { return m_segments; }
    [[nodiscard]] int runMaxSegments() const { return m_runMaxSegments; }
    [[nodiscard]] double error() const { return m_error; }
    [[nodiscard]] double initialError() const { return m_initialError; }
    [[nodiscard]] double elapsedSeconds() const { return m_elapsedSeconds; }
    [[nodiscard]] double threadLengthM() const { return m_threadLengthMm / 1000.0; }

signals:
    void pinCountChanged();
    void workingSizeChanged();
    void settingsChanged();
    void renderStyleChanged();
    void toneChanged();
    void imagesChanged();
    void stateChanged();
    void statusTextChanged();
    void resultRevisionChanged();
    void pinPositionsChanged();
    void progressChanged();

private:
    void updateBase();
    void updateProcessed();
    void updateResultImage();
    void onWorkerProgress();
    void onWorkerFinished(bool success);
    void setStatus(const QString &text);
    void setState(bool running, bool paused);
    [[nodiscard]] sa::RenderStyle renderStyle() const;

    std::shared_ptr<ImageStore> m_store;
    OptimizationWorker *m_worker = nullptr;

    int m_pinCount = 240;
    int m_workingSize = 512;
    int m_maxSegments = 4000;
    int m_minSeparation = 20;
    int m_startPin = 0;
    double m_opacity = 0.2;
    double m_threadWidth = 1.0;
    double m_diameterMm = 500.0;
    double m_maxLengthM = 0.0;
    bool m_allowRepeats = false;
    QColor m_threadColor = Qt::black;
    QColor m_backgroundColor = Qt::white;
    double m_brightness = 0.0;
    double m_contrast = 1.0;
    double m_gamma = 1.0;
    bool m_invert = false;
    int m_exportSize = 2048;
    bool m_csvMillimeters = true;

    QImage m_original;
    QString m_imageName;
    sa::GrayImage m_base;
    sa::GrayImage m_processed;
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

    // Layout and style of the run that produced m_resultSequence.
    std::optional<sa::PinLayout> m_resultLayout;
    sa::RenderStyle m_resultStyle;
    std::vector<int> m_resultSequence;
    std::vector<float> m_resultTransmittance;
};
