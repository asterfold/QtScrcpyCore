#include <QApplication>
#include <QFile>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QEventLoop>
#include <QTimer>
#include <QtEndian>
#include <iostream>
#include <stdexcept>
#include "controller.h"
#include "keymap.h"
static void require(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
struct Touch { int action; int x; int y; };
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try {
        require(argc == 2, "profile required");
        QFile file(QString::fromLocal8Bit(argv[1]));
        require(file.open(QIODevice::ReadOnly), "profile unavailable");
        const QString script = QString::fromUtf8(file.readAll());
        KeyMap parsed; parsed.loadKeyMap(script);
        const auto &node = parsed.getKeyMapNodeKey(KeyMap::WheelDownKey);
        require(node.type == KeyMap::KMT_CLICK && !node.data.click.switchMap, "wheel mapping rejected");
        QVector<Touch> events;
        Controller controller([&](const QByteArray &bytes) {
            if (bytes.size() >= 18 && bytes[0] == ControlMsg::CMT_INJECT_TOUCH)
                events.append({static_cast<unsigned char>(bytes[1]),
                    int(qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(bytes.constData()) + 10)),
                    int(qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(bytes.constData()) + 14))});
            return bytes.size();
        }, script);
        const QSize size(2000, 1200);
        auto drain = [&] { QCoreApplication::sendPostedEvents(&controller); };
        auto toggle = [&] {
            QKeyEvent key(QEvent::KeyPress, Qt::Key_QuoteLeft, Qt::NoModifier);
            controller.keyEvent(&key, size, size); drain();
        };
        auto wheel = [&](int vertical, int horizontal = 0) {
            QWheelEvent event(QPointF(100, 100), QPointF(100, 100), QPoint(), QPoint(horizontal, vertical),
                Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            controller.wheelEvent(&event, size, size); drain();
        };
        auto waitTap = [&] {
            QEventLoop loop; QTimer::singleShot(50, &loop, &QEventLoop::quit); loop.exec(); drain();
        };
        toggle(); events.clear();
        wheel(120); wheel(0, -120); wheel(0);
        require(events.isEmpty(), "up/horizontal/zero scroll activated mapping");
        wheel(-60); require(events.isEmpty(), "fractional scroll activated too early");
        wheel(-60); require(events.size() == 1 && events[0].action == 0, "detent missing touch down");
        require(events[0].x == 583 && events[0].y == 354, "wrong target position");
        wheel(-120); waitTap();
        require(events.size() == 2 && events[1].action == 1, "burst or release left duplicate/stuck touch");
        require(controller.isCurrentCustomKeymap(), "wheel released mouse capture");
        events.clear(); waitTap(); require(events.isEmpty(), "stationary wheel repeated tap");
        wheel(-120); toggle();
        require(events.size() == 2 && events.last().action == 1, "mode exit left touch held");
        events.clear(); waitTap(); require(events.isEmpty(), "stale callback emitted touch after exit");
        toggle(); wheel(-60); controller.updateScript(script); drain();
        if (!controller.isCurrentCustomKeymap()) toggle();
        events.clear(); wheel(-60); require(events.isEmpty(), "reload retained partial detent");
        wheel(-60); controller.updateScript(script); drain();
        require(events.size() == 2 && events.last().action == 1, "reload left touch held");
        events.clear(); waitTap(); require(events.isEmpty(), "stale callback survived reload");
        std::cout << "PASS: wheel direction, fractional detent, target, burst, idle, capture, mode exit and reload\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
