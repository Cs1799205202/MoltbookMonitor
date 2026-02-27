#include <QCoreApplication>
#include <QApplication>
#include <QQmlContext>
#include <QQmlApplicationEngine>
#include <QSystemTrayIcon>
#include <QStyle>

#include "app_version.h"
#include "monitor/monitorcontroller.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("MoltbookMonitor"));
    QCoreApplication::setApplicationName(QStringLiteral("Moltbook Agent Activity Monitor"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(MoltbookMonitor::BuildInfo::kAppVersion));

    MonitorController monitorController;
    QSystemTrayIcon trayIcon;
    const bool trayAvailable = QSystemTrayIcon::isSystemTrayAvailable();
    if (trayAvailable) {
        QIcon trayImage = app.style()->standardIcon(QStyle::SP_ComputerIcon);
        if (trayImage.isNull()) {
            trayImage = QIcon::fromTheme(QStringLiteral("dialog-information"));
        }
        trayIcon.setIcon(trayImage);
        trayIcon.setToolTip(QStringLiteral("Moltbook Agent Activity Monitor"));
        trayIcon.show();
    }

    QObject::connect(&monitorController, &MonitorController::notificationRaised, &app, [&trayIcon, trayAvailable](const QString &message) {
        if (!trayAvailable || !trayIcon.isVisible()) {
            return;
        }
        trayIcon.showMessage(QStringLiteral("Moltbook Inactivity Alert"), message, QSystemTrayIcon::Warning, 10000);
    });

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("monitorController"), &monitorController);
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("MoltbookMonitor", "Main");

    return app.exec();
}
