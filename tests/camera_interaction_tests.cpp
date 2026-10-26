#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QtEndian>
#include <iostream>
#include <stdexcept>
#include "controller.h"
#include "keymap.h"

static void require(bool ok, const char *why) { if (!ok) { throw std::runtime_error(why); } }
struct Touch { int action; quint64 id; int x; int y; };

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    try {
        require(argc == 2, "profile path required");
        QFile file(QString::fromLocal8Bit(argv[1]));
        require(file.open(QIODevice::ReadOnly), "profile unavailable");
        auto profile = QJsonDocument::fromJson(file.readAll()).object();
        const QString normalScript = QString::fromUtf8(QJsonDocument(profile).toJson());
        KeyMap parsed; parsed.loadKeyMap(QJsonDocument(profile).toJson());
        require(parsed.isValidMouseMoveMap(), "view mapping rejected");
        require(parsed.getMouseMoveMap().data.mouseMove.speedRatio == QPointF(6.5, 2.5), "sensitivity not halved");
        for (int code : {Qt::Key_M, Qt::Key_Tab}) {
            const auto &node = parsed.getKeyMapNodeKey(code);
            require(node.type == KeyMap::KMT_CLICK && node.data.click.switchMap, "menu must release camera");
        }
        // Force repeated touch-boundary renewals without moving the real desktop cursor.
        auto view = profile.value("mouseMoveMap").toObject();
        view["speedRatioX"] = 0.1; view["speedRatioY"] = 0.1;
        profile["mouseMoveMap"] = view;
        QVector<Touch> events; QVector<bool> captured;
        const QString script = QString::fromUtf8(QJsonDocument(profile).toJson());
        Controller controller([&](const QByteArray &bytes) {
            if (bytes.size() > 10 && bytes[0] == ControlMsg::CMT_INJECT_TOUCH) {
                events.append({static_cast<unsigned char>(bytes[1]), qFromBigEndian<quint64>(reinterpret_cast<const uchar *>(bytes.constData()) + 2),
                               int(qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(bytes.constData()) + 10)),
                               int(qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(bytes.constData()) + 14))});
            }
            return bytes.size();
        }, script);
        QObject::connect(&controller, &Controller::grabCursor, [&](bool grab) { captured.append(grab); });
        const QSize size(2000, 1200);
        auto drain = [&] { QCoreApplication::sendPostedEvents(&controller); };
        auto key = [&](int code, bool down, bool repeat = false) {
            QKeyEvent event(down ? QEvent::KeyPress : QEvent::KeyRelease, code, Qt::NoModifier, "", repeat);
            controller.keyEvent(&event, size, size); drain();
        };
        auto move = [&](double x, double y = 600) {
            QMouseEvent event(QEvent::MouseMove, QPointF(x, y), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            controller.mouseEvent(&event, size, size); drain();
        };
        key(Qt::Key_QuoteLeft, true);
        key(Qt::Key_QuoteLeft, true, true);
        require(controller.isCurrentCustomKeymap(), "repeat toggled camera off");
        move(500); events.clear();
        for (int i = 0; i < 600; ++i) {
            events.clear(); move(i % 2 ? 500 : 1500);
            bool moved = false;
            for (const auto &touch : events) {
                moved |= touch.action == 2;
                require(touch.x >= 0 && touch.x <= size.width() && touch.y >= 0 && touch.y <= size.height(),
                        "touch renewal released at an out-of-screen position");
            }
            require(moved, "movement discarded at touch boundary");
        }
        events.clear(); move(500);
        require(events.isEmpty(), "stationary mouse emitted touch");
        for (int menu : {Qt::Key_M, Qt::Key_Tab}) {
            key(menu, true); key(menu, false);
            require(!captured.last() && !QApplication::overrideCursor(), "menu did not release cursor");
            events.clear(); move(1500); move(500);
            require(events.isEmpty(), "hover in menu emitted touch");
            QMouseEvent down(QEvent::MouseButtonPress, QPointF(800, 600), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent up(QEvent::MouseButtonRelease, QPointF(800, 600), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            controller.mouseEvent(&down, size, size); controller.mouseEvent(&up, size, size); drain();
            require(events.size() == 2 && events[0].action == 0 && events[1].action == 1, "menu click broken");
            key(menu, true); key(menu, false);
            require(captured.last() && QApplication::overrideCursor(), "camera did not resume");
            events.clear(); move(500);
            require(events.isEmpty(), "resume used stale mouse position");
            move(1500);
            require(!events.isEmpty(), "camera movement missing after menu");
        }
        events.clear(); controller.updateScript(script); drain();
        require(!captured.last() && !QApplication::overrideCursor(), "script reload retained capture");
        bool released = false;
        for (const auto &touch : events) { released |= touch.action == 1; }
        require(released, "script reload left camera finger down");
        for (bool horizontal : {true, false}) {
            for (int direction : {-1, 1}) {
                QVector<Touch> edgeEvents;
                Controller edge([&](const QByteArray &bytes) {
                    if (bytes.size() > 18 && bytes[0] == ControlMsg::CMT_INJECT_TOUCH) {
                        edgeEvents.append({int(static_cast<unsigned char>(bytes[1])),
                            qFromBigEndian<quint64>(reinterpret_cast<const uchar *>(bytes.constData()) + 2),
                            int(qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(bytes.constData()) + 10)),
                            int(qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(bytes.constData()) + 14))});
                    }
                    return bytes.size();
                }, normalScript);
                QKeyEvent activate(QEvent::KeyPress, Qt::Key_QuoteLeft, Qt::NoModifier);
                edge.keyEvent(&activate, size, size);
                auto send = [&](QPointF point) {
                    QMouseEvent e(QEvent::MouseMove, point, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
                    edge.mouseEvent(&e, size, size); QCoreApplication::sendPostedEvents(&edge);
                };
                send(QPointF(1000, 600));
                const double extent = horizontal ? 2000 : 1200;
                double original = direction > 0 ? extent - 40 : 40;
                const double target = direction > 0 ? 50 : extent - 50;
                auto point = [&](double value) { return horizontal ? QPointF(value, 600) : QPointF(1000, value); };
                edgeEvents.clear(); send(point(original));
                require(!edgeEvents.isEmpty(), "edge movement missing");
                int previous = horizontal ? edgeEvents.last().x : edgeEvents.last().y;
                for (double value : {original + direction * 10, original + direction * 20, target + direction * 10}) {
                    edgeEvents.clear(); send(point(value));
                    require(!edgeEvents.isEmpty(), "queued edge movement missing");
                    const int current = horizontal ? edgeEvents.last().x : edgeEvents.last().y;
                    require((current - previous) * direction >= 0 && qAbs(current - previous) <= 5,
                            "queued pre-warp event produced a jump or reversal");
                    previous = current;
                }
            }
        }
        std::cout << "PASS: 600 bounded renewals, four edges with queued pre-warp events, stationary mouse, menus, cleanup, sensitivity\n";
    } catch (const std::exception &e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
