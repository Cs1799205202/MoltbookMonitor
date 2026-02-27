#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QFont>
#include <QMenu>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTimer>

#include "app_version.h"
#include "monitor/monitorcontroller.h"

namespace {

QIcon createEmojiTrayIcon(const QString &emoji, bool showUnreadBadge)
{
    constexpr int kIconSize = 64;
    QPixmap pixmap(kIconSize, kIconSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    QFont font = qApp->font();
#if defined(Q_OS_WIN)
    font.setFamilies({QStringLiteral("Segoe UI Emoji"), font.family()});
#elif defined(Q_OS_MACOS)
    font.setFamilies({QStringLiteral("Apple Color Emoji"), font.family()});
#else
    font.setFamilies({QStringLiteral("Noto Color Emoji"), font.family()});
#endif
    font.setPixelSize(46);
    painter.setFont(font);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, emoji);

    if (showUnreadBadge) {
        constexpr int kBadgeSize = 18;
        const QRect badgeRect(pixmap.width() - kBadgeSize - 4, 4, kBadgeSize, kBadgeSize);
        painter.setPen(QPen(Qt::white, 2));
        painter.setBrush(QColor(QStringLiteral("#ff3b30")));
        painter.drawEllipse(badgeRect);
    }

    return QIcon(pixmap);
}

void restoreMainWindow(QQuickWindow *mainWindow)
{
    if (!mainWindow) {
        return;
    }

    mainWindow->show();
    mainWindow->showNormal();
    mainWindow->raise();
    mainWindow->requestActivate();
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("MoltbookMonitor"));
    QCoreApplication::setApplicationName(QStringLiteral("Moltbook Agent Activity Monitor"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(MoltbookMonitor::BuildInfo::kAppVersion));
    const bool trayAvailable = QSystemTrayIcon::isSystemTrayAvailable();
    if (trayAvailable) {
        app.setQuitOnLastWindowClosed(false);
    }

    MonitorController monitorController;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("monitorController"), &monitorController);
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("MoltbookMonitor", "Main");
    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    auto *mainWindow = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (!mainWindow) {
        return -1;
    }

    QSystemTrayIcon trayIcon;
    QMenu trayMenu;
    QAction restoreAction(QObject::tr("Restore Main Window"), &trayMenu);
    QAction quitAction(QObject::tr("Exit"), &trayMenu);
    QTimer trayFlashTimer;
    trayFlashTimer.setInterval(450);

    const QString trayEmoji = QStringLiteral("🤖");
    QIcon trayNormalIcon = createEmojiTrayIcon(trayEmoji, false);
    if (trayNormalIcon.isNull()) {
        trayNormalIcon = app.style()->standardIcon(QStyle::SP_ComputerIcon);
    }
    QIcon trayAlertIcon = createEmojiTrayIcon(trayEmoji, true);
    if (trayAlertIcon.isNull()) {
        trayAlertIcon = trayNormalIcon;
    }
    app.setWindowIcon(trayNormalIcon);
    mainWindow->setIcon(trayNormalIcon);

    bool trayHasUnreadNotification = false;
    bool flashAlertFrame = false;
    bool quitRequested = false;
    bool closeHintShown = false;
    int unreadNotificationCount = 0;
    const QString baseTrayToolTip = QStringLiteral("Moltbook Agent Activity Monitor");

    const auto updateTrayToolTip = [&]() {
        if (!trayAvailable) {
            return;
        }
        if (unreadNotificationCount <= 0) {
            trayIcon.setToolTip(baseTrayToolTip);
            return;
        }
        trayIcon.setToolTip(QStringLiteral("%1 (%2 unread alerts)").arg(baseTrayToolTip).arg(unreadNotificationCount));
    };

    const auto clearTrayAttention = [&]() {
        trayHasUnreadNotification = false;
        flashAlertFrame = false;
        unreadNotificationCount = 0;
        trayFlashTimer.stop();
        if (trayAvailable) {
            trayIcon.setIcon(trayNormalIcon);
            updateTrayToolTip();
        }
    };

    const auto startTrayAttention = [&]() {
        if (!trayAvailable) {
            return;
        }
        trayHasUnreadNotification = true;
        flashAlertFrame = true;
        unreadNotificationCount++;
        trayIcon.setIcon(trayAlertIcon);
        updateTrayToolTip();
        if (!trayFlashTimer.isActive()) {
            trayFlashTimer.start();
        }
    };

    if (trayAvailable) {
        trayMenu.addAction(&restoreAction);
        trayMenu.addSeparator();
        trayMenu.addAction(&quitAction);
        trayIcon.setContextMenu(&trayMenu);
        trayIcon.setIcon(trayNormalIcon);
        trayIcon.setToolTip(baseTrayToolTip);
        trayIcon.show();

        QObject::connect(&trayFlashTimer, &QTimer::timeout, &app, [&]() {
            if (!trayHasUnreadNotification) {
                clearTrayAttention();
                return;
            }
            flashAlertFrame = !flashAlertFrame;
            trayIcon.setIcon(flashAlertFrame ? trayAlertIcon : trayNormalIcon);
        });

        QObject::connect(&restoreAction, &QAction::triggered, &app, [&]() {
            restoreMainWindow(mainWindow);
            clearTrayAttention();
        });

        QObject::connect(&trayIcon, &QSystemTrayIcon::activated, &app, [&](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick
                || reason == QSystemTrayIcon::MiddleClick) {
                restoreMainWindow(mainWindow);
                clearTrayAttention();
            }
        });

        QObject::connect(&trayIcon, &QSystemTrayIcon::messageClicked, &app, [&]() {
            restoreMainWindow(mainWindow);
            clearTrayAttention();
        });

        QObject::connect(mainWindow, &QWindow::activeChanged, &app, [&]() {
            if (mainWindow->isActive()) {
                clearTrayAttention();
            }
        });

        QObject::connect(mainWindow, &QQuickWindow::closing, &app, [&](QQuickCloseEvent *) {
            if (quitRequested) {
                return;
            }
            if (!closeHintShown && trayIcon.isVisible()) {
                closeHintShown = true;
                trayIcon.showMessage(QStringLiteral("Running in Background"),
                                     QStringLiteral("Moltbook Monitor is still running in the system tray."),
                                     QSystemTrayIcon::Information,
                                     5000);
            }
        });

        QObject::connect(&quitAction, &QAction::triggered, &app, [&]() {
            quitRequested = true;
            clearTrayAttention();
            trayIcon.hide();
            QCoreApplication::quit();
        });
    }

    QObject::connect(&monitorController, &MonitorController::notificationRaised, &app, [&](const QString &message) {
        if (!trayAvailable || !trayIcon.isVisible()) {
            return;
        }
        trayIcon.showMessage(QStringLiteral("Moltbook Inactivity Alert"), message, QSystemTrayIcon::Warning, 10000);
        if (!mainWindow->isActive() || !mainWindow->isVisible()) {
            startTrayAttention();
        }
    });

    return app.exec();
}
