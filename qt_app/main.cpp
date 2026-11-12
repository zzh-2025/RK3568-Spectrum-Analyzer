#include <QApplication>
#include <QMainWindow>
#include <QScreen>
#include <QRect>
#include <memory>

#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    std::unique_ptr<QMainWindow> window(createMainWindow());

    // Xfce's top panel occupies real vertical space on the RK3568 LCD.
    // Use availableGeometry() instead of screen geometry or showFullScreen().
    // Extra space below also accommodates the window frame / small WM offsets.
    const QScreen *screen = app.primaryScreen();
    if (screen) {
        const QRect available = screen->availableGeometry();
        if (available.width() <= 1280 || available.height() <= 800) {
            const int leftRight = 7;
            const int reserveForTitleBar = 24;
            const int reserveAtBottom = 30;
            QRect safe = available.adjusted(leftRight, reserveForTitleBar,
                                            -leftRight, -reserveAtBottom);
            if (safe.width() < 640 || safe.height() < 300)
                safe = available.adjusted(2, 20, -2, -20);
            window->setGeometry(safe);
        }
    }
    window->show();  // Do not maximize: preserve our safety margins.
    return app.exec();
}
