#pragma once

#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QPointer>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QWindow>

// GUI lifetime only: the separately managed daemon is never started or stopped.
class ResidentWindow : public QObject
{
    Q_OBJECT

public:
    explicit ResidentWindow(QObject *parent = nullptr) : QObject(parent), m_tray(this)
    {
        QApplication::setQuitOnLastWindowClosed(false);
        m_tray.setIcon(QIcon::fromTheme(QStringLiteral("flydigi-bs2pro-linux"),
            QApplication::style()->standardIcon(QStyle::SP_ComputerIcon)));
        m_tray.setToolTip(QStringLiteral("Space Station"));
        auto *show = m_menu.addAction(tr("Show Space Station"), this, &ResidentWindow::showWindow);
        show->setObjectName(QStringLiteral("showWindow"));
        m_menu.addSeparator();
        auto *quit = m_menu.addAction(tr("Quit"), this, &ResidentWindow::quit);
        quit->setObjectName(QStringLiteral("quitApplication"));
        m_tray.setContextMenu(&m_menu);
        connect(&m_tray, &QSystemTrayIcon::activated, this,
                [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
                showWindow();
        });
        // Qt also registers the icon if a tray host becomes available later.
        m_tray.show();
    }

    void setWindow(QWindow *window) { m_window = window; }

    Q_INVOKABLE void hideWindow()
    {
        if (!m_window)
            return;
        if (QSystemTrayIcon::isSystemTrayAvailable())
            m_window->hide();
        else
            m_window->showMinimized(); // Keep a task-switcher recovery path without a tray.
    }

    Q_INVOKABLE void showWindow()
    {
        if (!m_window)
            return;
        if (m_window->windowState() == Qt::WindowMinimized)
            m_window->showNormal();
        else
            m_window->show();
        m_window->raise();
        m_window->requestActivate();
    }

    Q_INVOKABLE void quit() { QCoreApplication::quit(); }

private:
    QPointer<QWindow> m_window;
    QMenu m_menu;
    QSystemTrayIcon m_tray;
};
