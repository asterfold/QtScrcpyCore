#include <QApplication>
#include <QCursor>
#include <QFile>
#include <QKeyEvent>
#include <QVector>
#include <iostream>
#include "controller.h"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (argc != 2) { return 1; }
    QFile profile(QString::fromLocal8Bit(argv[1]));
    if (!profile.open(QIODevice::ReadOnly)) { return 2; }
    QVector<bool> captured;
    Controller controller([](const QByteArray &bytes) { return bytes.size(); }, QString::fromUtf8(profile.readAll()));
    QObject::connect(&controller, &Controller::grabCursor, [&](bool grab) { captured.append(grab); });
    auto toggle = [&](QSize size) {
        QKeyEvent event(QEvent::KeyPress, Qt::Key_QuoteLeft, Qt::NoModifier);
        controller.keyEvent(&event, QSize(1920, 1200), size);
    };
    toggle(QSize(1080, 675));
    if (captured != QVector<bool>{true} || !QApplication::overrideCursor()
            || QApplication::overrideCursor()->shape() != Qt::BlankCursor) { return 3; }
    QKeyEvent event(QEvent::KeyRelease, Qt::Key_F24, Qt::NoModifier);
    controller.keyEvent(&event, QSize(1920, 1200), QSize(960, 600));
    if (captured != QVector<bool>({true, true})) { return 4; }
    toggle(QSize(960, 600));
    if (captured != QVector<bool>({true, true, false}) || QApplication::overrideCursor()) { return 5; }
    std::cout << "PASS: capture on entry, recapture on resize, hide cursor, release and restore on exit\n";
}
