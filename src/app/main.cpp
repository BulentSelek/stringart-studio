#include "ImageStore.h"
#include "ProjectController.h"

#include <QApplication>
#include <QDir>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QSettings>
#include <QtQml/QQmlExtensionPlugin>

Q_IMPORT_QML_PLUGIN(StringArtStudioPlugin)

int main(int argc, char *argv[])
{
    // Compact Material controls so each parameter page fits without scrolling.
    if (!qEnvironmentVariableIsSet("QT_QUICK_CONTROLS_MATERIAL_VARIANT"))
        qputenv("QT_QUICK_CONTROLS_MATERIAL_VARIANT", "Dense");
    // QApplication (not QGuiApplication): desktop platform themes such as KDE
    // Plasma only provide their native file dialogs to widget applications.
    // Without them Qt falls back to its QML dialog, whose Material sidebar
    // draws folder names in black on the dark theme.
    QApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("String Art Studio"));
    QGuiApplication::setOrganizationName(QStringLiteral("StringArtStudio"));
    QQuickStyle::setStyle(QStringLiteral("Material"));

    // The store outlives both owners: the controller (declared first, destroyed
    // last) and the image provider owned by the engine.
    auto store = std::make_shared<ImageStore>();
    ProjectController controller(store);
    {
        QSettings settings;
        controller.restoreSettings(settings);
    }
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &controller, [&controller] {
        QSettings settings;
        controller.saveSettings(settings);
    });

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("studio"), new StudioImageProvider(store));
    engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
        Qt::QueuedConnection);
    engine.loadFromModule("StringArtStudio", "Main");

    if (argc > 1)
        controller.loadImage(QUrl::fromUserInput(QString::fromLocal8Bit(argv[1]), QDir::currentPath(),
                                                 QUrl::AssumeLocalFile));

    return QGuiApplication::exec();
}
