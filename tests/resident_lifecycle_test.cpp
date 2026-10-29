#include "backend.h"
#include "main.h"

#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QTimer>
#include <QSignalSpy>
#include <QTest>
#include <memory>

class ResidentLifecycleTest : public QObject
{
    Q_OBJECT

    Backend backend; // Never started: lifecycle tests cannot reach a real daemon.
    std::unique_ptr<ResidentWindow> resident;
    std::unique_ptr<QQmlApplicationEngine> engine;

    QAction *trayAction(const QString &name)
    {
        auto *tray = resident->findChild<QSystemTrayIcon *>();
        return tray ? tray->contextMenu()->findChild<QAction *>(name) : nullptr;
    }
    QQuickWindow *window = nullptr;

private slots:
    void init()
    {
        resident = std::make_unique<ResidentWindow>();
        engine = std::make_unique<QQmlApplicationEngine>();
        engine->addImportPath("qrc:/qt/qml");
        engine->rootContext()->setContextProperty("backend", &backend);
        engine->rootContext()->setContextProperty("resident", resident.get());
        engine->load(QUrl("qrc:/qt/qml/FlydigiSpace/qml/Main.qml"));
        QVERIFY(!engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(window);
        resident->setWindow(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QTRY_VERIFY(window->isActive());
    }

    void cleanup()
    {
        engine.reset();
        resident.reset();
        window = nullptr;
    }

    void closeKeepsWindowRestorable()
    {
        QSignalSpy quit(engine.get(), &QQmlEngine::quit);
        // A rejected close keeps the QML window alive for subsequent restoration.
        QVERIFY(!window->close());
        if (QSystemTrayIcon::isSystemTrayAvailable())
            QVERIFY(!window->isVisible());
        else
            QCOMPARE(window->visibility(), QWindow::Minimized);
        QVERIFY(!QApplication::quitOnLastWindowClosed());
        QCOMPARE(quit.count(), 0);
        auto *show = trayAction("showWindow");
        QVERIFY(show);
        show->trigger();
        QTRY_COMPARE(window->visibility(), QWindow::Windowed);
    }

    void restoreHiddenAndMinimizedWindow()
    {
        auto *show = trayAction("showWindow");
        QVERIFY(show);
        QCOMPARE(show->text(), QString("Show Space Station"));
        window->hide();
        show->trigger();
        QTRY_VERIFY(window->isVisible());
        window->showMinimized();
        show->trigger();
        QTRY_COMPARE(window->visibility(), QWindow::Windowed);
        show->trigger(); // Repeated Show does not hide/toggle the window.
        QVERIFY(window->isVisible());
    }

    void explicitQuit_data()
    {
        QTest::addColumn<bool>("fromTray");
        QTest::newRow("tray-quit-while-hidden") << true;
        QTest::newRow("window-quit") << false;
    }

    void explicitQuit()
    {
        QFETCH(bool, fromTray);
        auto *quit = trayAction("quitApplication");
        QVERIFY(quit);
        QCOMPARE(quit->text(), QString("Quit"));
        auto *button = window->findChild<QQuickItem *>("quitApplication");
        QVERIFY(button);
        QCOMPARE(button->property("text").toString(), QString("Quit"));
        QSignalSpy quitting(qApp, &QCoreApplication::aboutToQuit);
        bool survivedHiddenWindow = false;
        QTimer watchdog;
        watchdog.setSingleShot(true);
        connect(&watchdog, &QTimer::timeout, this, [] { QCoreApplication::exit(99); });
        watchdog.start(1000);
        QTimer::singleShot(0, this, [this, fromTray] {
            if (fromTray)
                window->hide();
        });
        QTimer::singleShot(50, this, [&, this] {
            survivedHiddenWindow = !fromTray || !window->isVisible();
            if (fromTray) {
                quit->trigger();
            } else {
                const QPoint point = button->mapToScene(
                    QPointF(button->width() / 2, button->height() / 2)).toPoint();
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
            }
        });
        QCOMPARE(qApp->exec(), 0);
        QVERIFY(survivedHiddenWindow);
        QCOMPARE(quitting.count(), 1);
    }

    void startupStillShowsDashboard()
    {
        QVERIFY(window->isVisible());
        QCOMPARE(window->title(), QString("Space Station — Flydigi cooler"));
        QCOMPARE(backend.manual(), false);
        QCOMPARE(backend.connected(), false);
    }

    void escapeDoesNotQuit()
    {
        QSignalSpy quit(engine.get(), &QQmlEngine::quit);
        QTest::keyClick(window, Qt::Key_Escape);
        QCOMPARE(quit.count(), 0);
        QVERIFY(!window->isVisible() || window->visibility() == QWindow::Minimized);
    }
};

QTEST_MAIN(ResidentLifecycleTest)
#include "resident_lifecycle_test.moc"
