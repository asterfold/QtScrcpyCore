#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QEventLoop>
#include <QTimer>
#include <QtEndian>
#include <QSet>
#include <QFile>
#include <cmath>
#include <stdexcept>
#include <iostream>
#include "controller.h"
#include "keymap.h"

static void require(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
static QJsonObject point(double x, double y) { return {{"x", x}, {"y", y}}; }
static QString script(const QJsonObject &wheel, bool buttons = true) {
    QJsonArray nodes{wheel};
    if (buttons) {
        for (const auto &key : {"Key_F", "LeftButton"}) {
            nodes.append(QJsonObject{{"type", "KMT_CLICK"}, {"key", key}, {"switchMap", false},
                {"pos", point(.8, .8)}, {"touchRange", point(.01, .02)}});
        }
    }
    return QJsonDocument(QJsonObject{{"switchKey", "Key_QuoteLeft"}, {"keyMapNodes", nodes}}).toJson();
}
struct Touch { int action; quint64 id; QPoint pos; };
static void testProfile(const QString &path, int expectedNodes = 20) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "profile unavailable");
    const QByteArray bytes = file.readAll();
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(bytes, &error);
    require(error.error == QJsonParseError::NoError && doc.isObject(), "invalid profile JSON");
    KeyMap parsed; parsed.loadKeyMap(QString::fromUtf8(bytes));
    require(parsed.isValidMouseMoveMap(), "profile view mapping rejected");
    require(parsed.getMouseMoveMap().data.mouseMove.smallEyes.key == Qt::Key_Alt, "Alt mapping lost");
    const auto nodes = doc.object().value("keyMapNodes").toArray();
    require(expectedNodes >= 20 && nodes.size() == expectedNodes, "unexpected profile node count");
    QVector<Touch> events;
    // Validate the complete profile above, but exclude view cursor management
    // from this headless test of wheel and click messages.
    auto runtime = doc.object(); runtime.remove("mouseMoveMap");
    Controller controller([&](const QByteArray &data) -> qint64 {
        if (data.size() >= 18 && static_cast<unsigned char>(data[0]) == ControlMsg::CMT_INJECT_TOUCH) {
            const auto *p = reinterpret_cast<const uchar *>(data.constData());
            events.append({p[1], qFromBigEndian<quint64>(p + 2),
                QPoint(qFromBigEndian<quint32>(p + 10), qFromBigEndian<quint32>(p + 14))});
        }
        return data.size();
    }, QJsonDocument(runtime).toJson());
    const QSize size(2400, 1080);
    auto key = [&](int code, bool down, bool repeat = false) {
        QKeyEvent event(down ? QEvent::KeyPress : QEvent::KeyRelease, code, Qt::NoModifier, "", repeat);
        controller.keyEvent(&event, size, size); QCoreApplication::sendPostedEvents(&controller);
    };
    auto position = [&](QPointF p) { return QPointF(p.x() * size.width(), p.y() * size.height()); };
    auto near = [&](QPoint actual, QPointF expected) {
        require(std::abs(actual.x() - expected.x()) <= 1 && std::abs(actual.y() - expected.y()) <= 1,
                "profile relative displacement changed");
    };
    key(parsed.getSwitchKey(), true);
    const auto &wheel = parsed.getKeyMapNodeKey(Qt::Key_W);
    require(wheel.type == KeyMap::KMT_STEER_WHEEL && wheel.data.steerWheel.useFloatingOrigin,
            "profile wheel rejected or disabled");
    const auto &w = wheel.data.steerWheel;
    for (int i = 0; i < 64; ++i) {
        events.clear(); key(Qt::Key_W, true);
        require(events.size() == 2 && events[0].action == 0, "profile wheel down missing");
        const QPoint origin = events[0].pos;
        const QPointF center = position(w.centerPos), range = position(w.floatingOriginRange);
        require(std::abs(origin.x() - center.x()) <= range.x() + .5
            && std::abs(origin.y() - center.y()) <= range.y() + .5, "profile origin outside range");
        near(events.last().pos - origin, position(QPointF(0, -w.up.extendOffset)));
        key(Qt::Key_W, true, true);
        require(events.size() == 2, "repeat resampled profile origin");
        key(Qt::Key_D, true);
        near(events.last().pos - origin, position(QPointF(w.right.extendOffset, -w.up.extendOffset)));
        key(Qt::Key_W, false);
        near(events.last().pos - origin, position(QPointF(w.right.extendOffset, 0)));
        key(Qt::Key_D, false);
        require(events.size() == 5 && events.last().action == 1, "profile wheel up missing");
        for (const auto &event : events) require(event.id == events[0].id, "profile pointer changed");
    }
    if (w.sprintKey != Qt::Key_unknown) {
        require(parsed.getKeyMapNodeKey(w.sprintKey).type == KeyMap::KMT_STEER_WHEEL,
                "sprint key not routed to wheel");
        for (int i = 0; i < 16; ++i) {
            events.clear();
            key(w.sprintKey, true);
            require(events.isEmpty(), "sprint alone started touch");
            key(Qt::Key_W, true);
            require(events.size() == 2, "sprint-first down/move missing");
            const QPoint origin = events[0].pos;
            const quint64 id = events[0].id;
            near(events.last().pos - origin, position(QPointF(0, -w.sprintUpOffset)));
            key(w.sprintKey, true, true);
            key(Qt::Key_W, true, true);
            require(events.size() == 2, "sprint repeat generated touch");
            key(Qt::Key_D, true);
            key(w.sprintKey, false);
            near(events.last().pos - origin, position(QPointF(w.right.extendOffset, -w.up.extendOffset)));
            key(w.sprintKey, true);
            near(events.last().pos - origin, position(QPointF(w.right.extendOffset, -w.sprintUpOffset)));
            key(Qt::Key_D, false);
            key(Qt::Key_W, false);
            require(events.size() == 7 && events.last().action == 1, "W release while sprint held did not lift");
            for (int j = 1; j < events.size() - 1; ++j) {
                require(events[j].action == 2 && events[j].id == id, "sprint split gesture or pointer");
            }
            require(events.last().id == id, "sprint up pointer changed");
            events.clear();
            QEventLoop hold; QTimer::singleShot(40, &hold, &QEventLoop::quit); hold.exec();
            QCoreApplication::sendPostedEvents(&controller);
            require(events.isEmpty(), "delayed sprint after W release");
            // Still-held Shift applies to the next W gesture, but its release restores walk.
            key(Qt::Key_W, true);
            const QPoint nextOrigin = events[0].pos;
            near(events.last().pos - nextOrigin, position(QPointF(0, -w.sprintUpOffset)));
            key(w.sprintKey, false);
            near(events.last().pos - nextOrigin, position(QPointF(0, -w.up.extendOffset)));
            key(Qt::Key_W, false);
            events.clear(); key(Qt::Key_W, true);
            const QPoint walkOrigin = events[0].pos;
            near(events.last().pos - walkOrigin, position(QPointF(0, -w.up.extendOffset)));
            key(w.sprintKey, true);
            near(events.last().pos - walkOrigin, position(QPointF(0, -w.sprintUpOffset)));
            const int count = events.size();
            QEventLoop quiet; QTimer::singleShot(40, &quiet, &QEventLoop::quit); quiet.exec();
            QCoreApplication::sendPostedEvents(&controller);
            require(events.size() == count, "held sprint drifted");
            key(Qt::Key_W, false); key(w.sprintKey, false);
            require(events.size() == count + 1 && events.last().action == 1, "sprint key release added touch");
        }
        key(w.sprintKey, true); key(Qt::Key_W, true);
        key(parsed.getSwitchKey(), true);
        require(events.last().action == 1, "toggle did not release sprint");
        key(parsed.getSwitchKey(), true); events.clear(); key(Qt::Key_W, true);
        near(events.last().pos - events[0].pos, position(QPointF(0, -w.up.extendOffset)));
        key(Qt::Key_W, false);
        key(w.sprintKey, true); key(Qt::Key_W, true);
        controller.updateScript(QJsonDocument(runtime).toJson());
        QCoreApplication::sendPostedEvents(&controller);
        require(events.last().action == 1, "reload did not release sprint");
        key(parsed.getSwitchKey(), true); events.clear(); key(Qt::Key_W, true);
        near(events.last().pos - events[0].pos, position(QPointF(0, -w.up.extendOffset)));
        key(Qt::Key_W, false);
        const auto sourceWheel = nodes[0].toObject();
        for (int invalidCase = 0; invalidCase < 5; ++invalidCase) {
            auto invalid = sourceWheel;
            if (invalidCase == 0) invalid["sprintUpOffset"] = 1.0;
            if (invalidCase == 1) invalid["sprintKey"] = "Key_W";
            if (invalidCase == 2) invalid.remove("sprintUpOffset");
            if (invalidCase == 3) invalid.remove("floatingOriginRange");
            if (invalidCase == 4) invalid["sprintUpOffset"] = -0.1;
            KeyMap rejected; rejected.loadKeyMap(script(invalid, false));
            require(rejected.getKeyMapNodeKey(Qt::Key_W).type == KeyMap::KMT_INVALID,
                    "invalid sprint configuration accepted");
        }
        std::cout << "PASS: sprint modifier, both press orders, same pointer/origin, restore walk, W release, held Shift, no drift, toggle/reload reset, invalid bounds\n";
    }
    int buttons = 0;
    for (const auto value : nodes) {
        const auto node = value.toObject();
        if (node.value("type") != "KMT_CLICK") continue;
        ++buttons;
        const QByteArray name = node.value("key").toString().toLatin1();
        const bool mouse = name.endsWith("Button");
        const int code = mouse ? QMetaEnum::fromType<Qt::MouseButtons>().keyToValue(name.constData())
                               : QMetaEnum::fromType<Qt::Key>().keyToValue(name.constData());
        require(code >= 0, "unknown key in profile");
        const auto &mapped = mouse ? parsed.getKeyMapNodeMouse(code) : parsed.getKeyMapNodeKey(code);
        require(mapped.type == KeyMap::KMT_CLICK && mapped.data.click.useTouchRange, "profile button rejected or disabled");
        for (int i = 0; i < 8; ++i) {
            events.clear();
            for (bool down : {true, false}) {
                if (mouse) {
                    QMouseEvent event(down ? QEvent::MouseButtonPress : QEvent::MouseButtonRelease,
                        QPointF(), static_cast<Qt::MouseButton>(code),
                        down ? Qt::MouseButtons(code) : Qt::NoButton, Qt::NoModifier);
                    controller.mouseEvent(&event, size, size); QCoreApplication::sendPostedEvents(&controller);
                } else key(code, down);
            }
            require(events.size() == 2 && events[0].action == 0 && events[1].action == 1
                    && events[0].id == events[1].id && events[0].pos == events[1].pos, "profile click not stable");
            const auto pos = position(mapped.data.click.keyNode.pos), range = position(mapped.data.click.touchRange);
            require(std::abs(events[0].pos.x() - pos.x()) <= range.x() + .5
                && std::abs(events[0].pos.y() - pos.y()) <= range.y() + .5, "profile button outside range");
        }
    }
    require(buttons == expectedNodes - 1, "missing profile buttons");
    key(parsed.getSwitchKey(), true);
    controller.updateScript(QString::fromUtf8(bytes));
    key(parsed.getSwitchKey(), true);
    auto move = [&](double x, double y) {
        QMouseEvent event(QEvent::MouseMove, QPointF(x, y), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        controller.mouseEvent(&event, size, size);
        QCoreApplication::sendPostedEvents(&controller);
    };
    auto wait = [&]() {
        QEventLoop loop; QTimer::singleShot(90, &loop, &QEventLoop::quit); loop.exec();
        QCoreApplication::sendPostedEvents(&controller);
    };
    const auto &view = parsed.getMouseMoveMap().data.mouseMove;
    auto inRange = [&](QPoint actual, QPointF center, QPointF range) {
        center = position(center); range = position(range);
        require(std::abs(actual.x() - center.x()) <= range.x() + .5
            && std::abs(actual.y() - center.y()) <= range.y() + .5, "view origin outside range");
    };
    events.clear(); move(500, 500); move(520, 510);
    require(events.size() == 2 && events[0].action == 0, "view down missing");
    const QPoint origin = events[0].pos;
    inRange(origin, view.startPos, view.startPosRange);
    move(540, 520);
    require(events.size() == 3 && events.last().action == 2, "view resampled during drag");
    near(events.last().pos - origin, QPointF(40 / view.speedRatio.x(), 20 / view.speedRatio.y()));
    key(Qt::Key_Alt, true); wait();
    require(events.last().action == 0, "Alt down missing");
    inRange(events.last().pos, view.smallEyes.pos, view.smallEyesRange);
    const QPoint altOrigin = events.last().pos;
    move(560, 530);
    near(events.last().pos - altOrigin, QPointF(20 / view.speedRatio.x(), 10 / view.speedRatio.y()));
    key(Qt::Key_Alt, false);
    require(events.last().action == 0, "view resume missing");
    inRange(events.last().pos, view.startPos, view.startPosRange);
    key(Qt::Key_Alt, true);
    key(parsed.getSwitchKey(), true); events.clear(); wait();
    require(events.isEmpty(), "delayed view touch after mapping disabled");
    std::cout << "PASS: real profile, " << nodes.size()
              << " nodes, view/Alt parsed, 64 wheel gestures and " << buttons << " buttons x 8 clicks\n";
}
// Real profile: repeated tiny relative deltas must keep moving the camera,
// and neither camera resets nor mouse clicks may lift the held W pointer.
static void testRelativeInput(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "relative profile unavailable");
    const QByteArray profile = file.readAll();
    const auto viewConfig = QJsonDocument::fromJson(profile).object().value("mouseMoveMap").toObject();
    const auto configuredBounds = viewConfig.value("dragBounds").toObject();
    const bool checkBounds = !configuredBounds.isEmpty();
    const QRectF dragBounds(QPointF(configuredBounds.value("left").toDouble(), configuredBounds.value("top").toDouble()),
                           QPointF(configuredBounds.value("right").toDouble(), configuredBounds.value("bottom").toDouble()));
    if (checkBounds) {
        KeyMap parsed;
        parsed.loadKeyMap(QString::fromUtf8(profile));
        require(parsed.isValidMouseMoveMap() && parsed.getMouseMoveMap().data.mouseMove.dragBounds == dragBounds,
            "configured safe area was ignored");
        for (const auto &bad : {QJsonObject{{"left", -.1}, {"top", .2}, {"right", .7}, {"bottom", .45}},
                               QJsonObject{{"left", .7}, {"top", .2}, {"right", .45}, {"bottom", .45}},
                               QJsonObject{{"left", .45}, {"top", .6}, {"right", .7}, {"bottom", .8}},
                               QJsonObject{{"left", "bad"}, {"top", .2}, {"right", .7}, {"bottom", .45}}}) {
            auto doc = QJsonDocument::fromJson(profile).object();
            auto view = doc.value("mouseMoveMap").toObject();
            view["dragBounds"] = bad; doc["mouseMoveMap"] = view;
            KeyMap rejected; rejected.loadKeyMap(QJsonDocument(doc).toJson());
            require(!rejected.isValidMouseMoveMap(), "invalid safe area accepted");
        }
    }
    QVector<Touch> events;
    QVector<quint16> pressures;
    Controller controller([&](const QByteArray &data) -> qint64 {
        if (data.size() >= 24 && static_cast<unsigned char>(data[0]) == ControlMsg::CMT_INJECT_TOUCH) {
            const auto *p = reinterpret_cast<const uchar *>(data.constData());
            events.append({p[1], qFromBigEndian<quint64>(p + 2),
                QPoint(qFromBigEndian<quint32>(p + 10), qFromBigEndian<quint32>(p + 14))});
            pressures.append(qFromBigEndian<quint16>(p + 22));
        }
        return data.size();
    }, QString::fromUtf8(profile));
    const QSize size(2400,1080);
    auto flush = [&] { QCoreApplication::sendPostedEvents(&controller); };
    auto key = [&](int code, bool down, bool repeat = false) {
        QKeyEvent event(down ? QEvent::KeyPress : QEvent::KeyRelease, code, Qt::NoModifier, "", repeat);
        controller.keyEvent(&event, size, size); flush();
    };
    auto relative = [&](QPointF delta) {
        const int begin = events.size();
        QMouseEvent event(QEvent::MouseMove, delta, delta, QPointF(500,500),
            Qt::NoButton, Qt::NoButton, Qt::NoModifier, Qt::MouseEventSynthesizedByApplication);
        controller.mouseEvent(&event, size, size); flush();
        if (checkBounds) {
            for (int i=begin;i<events.size();++i) {
                const QPointF normalized(double(events[i].pos.x())/size.width(),double(events[i].pos.y())/size.height());
                require(dragBounds.adjusted(-.001,-.001,.001,.001).contains(normalized),
                    "camera touch escaped safe area into mapped buttons");
            }
        }
    };
    key(Qt::Key_QuoteLeft,true);
    key(Qt::Key_W,true);
    require(events.size()==2, "held W down/move missing");
    const quint64 wheel = events.first().id;
    {
        QEventLoop stationary;
        QTimer::singleShot(10000,&stationary,&QEventLoop::quit);
        stationary.exec(); flush();
        require(events.size()==2 && pressures[0]>0 && pressures[1]>0,
            "stationary W released or lost pressure without another key");
    }
    relative(QPointF(2,1));
    const QPoint firstCamera = events.last().pos;
    for (int i=0;i<100;++i) {
        key(Qt::Key_W,false,true); key(Qt::Key_W,true,true);
        relative(QPointF(2,1));
    }
    require(events.last().pos.x() > firstCamera.x()+10, "repeated tiny relative movement stalled");
    for (int i=0;i<20;++i) {
        relative(QPointF(9000,-9000)); relative(QPointF(-9000,9000));
        for (bool down : {true,false}) {
            QMouseEvent click(down ? QEvent::MouseButtonPress : QEvent::MouseButtonRelease,
                QPointF(500,500),Qt::LeftButton,down ? Qt::LeftButton : Qt::NoButton,Qt::NoModifier);
            controller.mouseEvent(&click,size,size); flush();
        }
    }
    QEventLoop hold; QTimer::singleShot(1200,&hold,&QEventLoop::quit); hold.exec(); flush();
    for (int i=0;i<events.size();++i) {
        require(events[i].id != wheel || events[i].action != 1, "camera/click interrupted held W");
        require(events[i].action == 1 ? pressures[i] == 0 : pressures[i] > 0,
            "active touch lost pressure before release");
    }
    key(Qt::Key_W,false);
    require(events.last().id==wheel && events.last().action==1, "physical W release missing");
    key(Qt::Key_QuoteLeft,true);
    std::cout << "PASS: real profile relative micro-movement, large camera resets, held W, repeat release filtering, clicks, active pressure\n";
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try {
        if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--relative") {
            testRelativeInput(QString::fromLocal8Bit(argv[2])); return 0;
        }
        if (argc == 2 || argc == 3) {
            testProfile(QString::fromLocal8Bit(argv[1]), argc == 3 ? std::stoi(argv[2]) : 20);
            return 0;
        }
        QJsonObject wheel{{"type", "KMT_STEER_WHEEL"}, {"centerPos", point(.3, .5)},
            {"leftKey", "Key_A"}, {"rightKey", "Key_D"}, {"upKey", "Key_W"}, {"downKey", "Key_S"},
            {"leftOffset", .1}, {"rightOffset", .1}, {"upOffset", .1}, {"downOffset", .1},
            {"floatingOriginRange", point(.02, .03)}};
        QVector<Touch> events;
        Controller controller([&](const QByteArray &data) -> qint64 {
            if (data.size() >= 18 && static_cast<unsigned char>(data[0]) == ControlMsg::CMT_INJECT_TOUCH) {
                const auto *p = reinterpret_cast<const uchar *>(data.constData());
                events.append({p[1], qFromBigEndian<quint64>(p + 2),
                    QPoint(qFromBigEndian<quint32>(p + 10), qFromBigEndian<quint32>(p + 14))});
            }
            return data.size();
        }, script(wheel));
        auto flush = [&] { QCoreApplication::sendPostedEvents(&controller); };
        auto key = [&](int code, bool down, bool repeat = false) {
            QKeyEvent event(down ? QEvent::KeyPress : QEvent::KeyRelease, code, Qt::NoModifier, "", repeat);
            controller.keyEvent(&event, QSize(1000, 1000), QSize(1000, 1000)); flush();
        };
        auto quiet = [&] {
            const int count = events.size();
            QEventLoop loop; QTimer::singleShot(35, &loop, &QEventLoop::quit); loop.exec(); flush();
            require(events.size() == count, "unexpected delayed touch / hold drift");
        };
        auto delta = [&](QPoint value, QPoint expected) {
            require((value - expected).manhattanLength() <= 1, "relative movement changed");
        };
        key(Qt::Key_QuoteLeft, true);
        key(Qt::Key_W, true);
        require(events.size() == 2 && events[0].action == 0 && events[1].action == 2, "expected down then move");
        const QPoint origin = events[0].pos;
        const quint64 id = events[0].id;
        delta(events.last().pos - origin, QPoint(0, -100));
        key(Qt::Key_W, true, true); quiet();
        require(events.size() == 2, "auto repeat changed origin");
        key(Qt::Key_D, true); delta(events.last().pos - origin, QPoint(100, -100));
        key(Qt::Key_W, false); delta(events.last().pos - origin, QPoint(100, 0));
        key(Qt::Key_D, false);
        require(events.size() == 5 && events.last().action == 1, "final release missing");
        for (const auto &event : events) require(event.id == id, "pointer changed during gesture");
        quiet();

        QSet<QString> origins;
        for (int i = 0; i < 128; ++i) {
            events.clear(); key(Qt::Key_W, true); key(Qt::Key_W, false);
            require(events.size() == 3 && events.last().action == 1, "quick tap did not release");
            const QPoint p = events[0].pos;
            require(p.x() >= 280 && p.x() <= 320 && p.y() >= 470 && p.y() <= 530, "origin out of range");
            delta(events[1].pos - p, QPoint(0, -100));
            origins.insert(QString::number(p.x()) + "," + QString::number(p.y()));
        }
        require(origins.size() > 1, "origin never changes");
        events.clear(); key(Qt::Key_W, true); key(Qt::Key_S, true);
        delta(events.last().pos - events[0].pos, QPoint(0, 0));
        key(Qt::Key_W, false); delta(events.last().pos - events[0].pos, QPoint(0, 100));
        key(Qt::Key_S, false);

        events.clear(); key(Qt::Key_F, true); key(Qt::Key_F, true, true); quiet(); key(Qt::Key_F, false);
        require(events.size() == 2 && events[0].pos == events[1].pos, "button did not hold its point");
        require(events[0].pos.x() >= 790 && events[0].pos.x() <= 810
            && events[0].pos.y() >= 780 && events[0].pos.y() <= 820, "button out of range");
        events.clear();
        for (bool down : {true, false}) {
            QMouseEvent mouse(down ? QEvent::MouseButtonPress : QEvent::MouseButtonRelease,
                QPointF(0, 0), Qt::LeftButton, down ? Qt::LeftButton : Qt::NoButton, Qt::NoModifier);
            controller.mouseEvent(&mouse, QSize(1000, 1000), QSize(1000, 1000)); flush();
        }
        require(events.size() == 2 && events[0].pos == events[1].pos, "mouse button did not hold its point");

        events.clear(); key(Qt::Key_W, true); key(Qt::Key_F, true); key(Qt::Key_QuoteLeft, true);
        require(events.size() == 5 && events[3].action == 1 && events[4].action == 1, "toggle did not release touches");
        quiet(); key(Qt::Key_QuoteLeft, true);
        events.clear(); key(Qt::Key_W, true); controller.updateScript(script(wheel)); flush();
        require(events.size() == 3 && events.last().action == 1, "reload did not release wheel");

        for (const auto &range : {point(-.01, 0), point(.5, .5), QJsonObject{{"x", "bad"}, {"y", 0}}}) {
            QJsonObject invalid = wheel; invalid["floatingOriginRange"] = range;
            KeyMap map; map.loadKeyMap(script(invalid, false));
            require(map.getKeyMapNodeKey(Qt::Key_W).type == KeyMap::KMT_INVALID, "invalid range accepted");
        }
        QJsonObject legacy = wheel; legacy.remove("floatingOriginRange");
        KeyMap oldMap; oldMap.loadKeyMap(script(legacy, false));
        require(!oldMap.getKeyMapNodeKey(Qt::Key_W).data.steerWheel.useFloatingOrigin, "legacy mapping opted in");
        std::cout << "PASS: gesture stability, 128 bounded taps, combinations, keyboard/mouse clicks, toggle/reload release, validation, legacy defaults\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}
