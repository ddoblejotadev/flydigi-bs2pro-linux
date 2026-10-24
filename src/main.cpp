#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "backend.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("flydigi-space"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Space Station"));
    QGuiApplication::setOrganizationName(QStringLiteral("flydigi-space"));
    QGuiApplication::setDesktopFileName(QStringLiteral("flydigi-space"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Flydigi BS series cooler dashboard"));
    parser.addHelpOption();
    QCommandLineOption socketOption(
        {QStringLiteral("s"), QStringLiteral("socket")},
        QStringLiteral("Daemon socket (default: /run/flydigictl/flydigictl.sock, then $XDG_RUNTIME_DIR/flydigictl.sock)"),
        QStringLiteral("path"));
    parser.addOption(socketOption);
    parser.process(app);

    Backend backend;
    if (parser.isSet(socketOption))
        backend.setSocketPath(parser.value(socketOption));
    backend.start();

    QQmlApplicationEngine engine;

    // Qt looks for the QML module next to the binary only in a build tree. An
    // installed copy keeps it under share, so point the engine there.
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList importRoots{
        appDir + QStringLiteral("/../share/flydigi-space/qml"),
        appDir + QStringLiteral("/../lib/flydigi-space/qml"),
        appDir,
    };
    for (const QString &root : importRoots) {
        if (QFileInfo::exists(root + QStringLiteral("/FlydigiSpace/qmldir")))
            engine.addImportPath(root);
    }

    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.loadFromModule(QStringLiteral("FlydigiSpace"), QStringLiteral("Main"));

    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
