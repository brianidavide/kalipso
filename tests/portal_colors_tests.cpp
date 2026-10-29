// SPDX-License-Identifier: AGPL-3.0-or-later
#include "screen_colors/pixels.h"
#include "screen_colors/portal.h"
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusUnixFileDescriptor>
#include <QDBusVirtualObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

struct TestStream {
    uint node;
    QVariantMap properties;
};
Q_DECLARE_METATYPE(TestStream)
Q_DECLARE_METATYPE(QList<TestStream>)
QDBusArgument &operator<<(QDBusArgument &a, const TestStream &s) {
    a.beginStructure();
    a << s.node << s.properties;
    a.endStructure();
    return a;
}
const QDBusArgument &operator>>(const QDBusArgument &a, TestStream &s) {
    a.beginStructure();
    a >> s.node >> s.properties;
    a.endStructure();
    return a;
}

class FakePortal final : public QDBusVirtualObject {
    Q_OBJECT
public:
    QString session, request;
    QStringList calls, closed;
    uint responseCode = 0;
    bool hold = false;
    uint streamNode = 0;
    QVariantMap selection;
    QString introspect(const QString &) const override { return {}; }
    void respond(const QString &target, uint code, const QVariantMap &result) {
        auto event = QDBusMessage::createSignal(target, "org.freedesktop.portal.Request", "Response");
        event.setArguments({code, result});
        QDBusConnection::sessionBus().send(event);
    }
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &bus) override {
        if (message.interface() != "org.freedesktop.portal.ScreenCast" &&
            message.interface() != "org.freedesktop.portal.Request" &&
            message.interface() != "org.freedesktop.portal.Session") {
            bus.send(message.createErrorReply(QDBusError::UnknownInterface,
                                              "Only ScreenCast is provided by this test portal"));
            return true;
        }
        const auto method = message.member();
        calls.append(method);
        if (method == "Close") {
            closed.append(message.path());
            bus.send(message.createReply());
            return true;
        }
        if (method == "OpenPipeWireRemote") {
            if (!qEnvironmentVariableIsSet("KALIPSO_PIPEWIRE_TEST")) {
                bus.send(message.createErrorReply(QDBusError::Failed, "Deliberate stream failure"));
                return true;
            }
            const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
            sockaddr_un address{};
            address.sun_family = AF_UNIX;
            const auto socketPath = qEnvironmentVariable("XDG_RUNTIME_DIR").toUtf8() + "/pipewire-0";
            if (fd < 0 || socketPath.size() >= qsizetype(sizeof(address.sun_path))) {
                if (fd >= 0)
                    ::close(fd);
                return false;
            }
            std::memcpy(address.sun_path, socketPath.constData(), size_t(socketPath.size() + 1));
            if (::connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) {
                ::close(fd);
                return false;
            }
            QDBusUnixFileDescriptor remote;
            remote.giveFileDescriptor(fd);
            bus.send(message.createReply(QVariant::fromValue(remote)));
            return true;
        }
        auto who = message.service().mid(1);
        who.replace('.', '_');
        const auto options = qdbus_cast<QVariantMap>(message.arguments().last());
        request = "/org/freedesktop/portal/desktop/request/" + who + "/" + options.value("handle_token").toString();
        QVariantMap result;
        if (method == "CreateSession") {
            session = "/org/freedesktop/portal/desktop/session/" + who + "/" +
                      options.value("session_handle_token").toString();
            result.insert("session_handle", session);
        } else if (method == "SelectSources")
            selection = options;
        else if (method == "Start" && streamNode)
            result.insert("streams", QVariant::fromValue(QList<TestStream>{{streamNode, {{"source_type", uint(2)}}}}));
        bus.send(message.createReply(QVariant::fromValue(QDBusObjectPath(request))));
        if (!hold) {
            const auto target = request;
            const auto code = responseCode;
            QTimer::singleShot(0, this, [=, this] { respond(target, code, result); });
        }
        return true;
    }
    void reset() {
        calls.clear();
        closed.clear();
        selection.clear();
        responseCode = 0;
        hold = false;
        streamNode = 0;
    }
};

class PortalTests : public QObject {
    Q_OBJECT
    FakePortal portal;
private slots:
    void initTestCase() {
        qDBusRegisterMetaType<TestStream>();
        qDBusRegisterMetaType<QList<TestStream>>();
        auto bus = QDBusConnection::sessionBus();
        QVERIFY(bus.registerService("org.freedesktop.portal.Desktop"));
        QVERIFY(bus.registerVirtualObject("/org/freedesktop/portal/desktop", &portal, QDBusConnection::SubPath));
    }
    void init() {
        QTest::qWait(30);
        portal.reset();
    }
    void sparsePixelsRespectFormatStrideAndBounds() {
        const unsigned char rgb[] = {255, 0, 0, 255, 0, 255, 0, 255, 9, 9, 9, 9, 0, 0, 255, 255, 255, 255, 255, 255};
        QCOMPARE(ScreenColors::average(rgb, sizeof(rgb), 2, 2, 12, false), QColor(127, 127, 127));
        QCOMPARE(ScreenColors::average(rgb, 4, 1, 1, 4, true), QColor(0, 0, 255));
        QCOMPARE(ScreenColors::average(rgb, 4, 1, 1, 4, false), QColor(255, 0, 0));
        QVERIFY(!ScreenColors::average(rgb, sizeof(rgb) - 1, 2, 2, 12, false).isValid());
        QVERIFY(!ScreenColors::average(rgb, sizeof(rgb), 2, 2, -12, false).isValid());
        QVERIFY(!ScreenColors::average(rgb, sizeof(rgb), 2, 2, 4, false).isValid());
        QVERIFY(!ScreenColors::average(nullptr, 4, 1, 1, 4, false).isValid());
    }
    void constructionNeverRequestsPermission() {
        PortalColors colors;
        QTest::qWait(40);
        QVERIFY(portal.calls.isEmpty());
        QVERIFY(!colors.active());
    }
    void cancellationDoesNotRetry() {
        portal.responseCode = 1;
        PortalColors colors;
        colors.start();
        QTRY_VERIFY(!colors.active());
        QVERIFY(colors.status().contains("cancelled"));
        QTRY_VERIFY(portal.closed.contains(portal.session));
        const auto count = portal.calls.count("CreateSession");
        QTest::qWait(100);
        QCOMPARE(portal.calls.count("CreateSession"), count);
    }
    void onlyAWindowIsRequestedWithoutPersistenceOrCursor() {
        PortalColors colors;
        colors.start();
        QTRY_VERIFY(portal.calls.contains("Start"));
        QCOMPARE(portal.selection.value("types").toUInt(), 2u);
        QCOMPARE(portal.selection.value("cursor_mode").toUInt(), 1u);
        QCOMPARE(portal.selection.value("multiple").toBool(), false);
        QVERIFY(!portal.selection.contains("persist_mode"));
        QVERIFY(!portal.selection.contains("restore_token"));
        QTRY_VERIFY(!colors.active());
        QVERIFY(colors.status().contains("Choose one"));
    }
    void streamFailureClosesSessionAndAllowsRetry() {
        portal.streamNode = 999;
        PortalColors colors;
        colors.start();
        QTRY_VERIFY(portal.calls.contains("OpenPipeWireRemote"));
        QTRY_VERIFY(!colors.active());
        QVERIFY(colors.status().contains("could not open"));
        QTRY_VERIFY(portal.closed.contains(portal.session));
        portal.responseCode = 1;
        colors.start();
        QTRY_COMPARE(portal.calls.count("CreateSession"), 2);
        QTRY_VERIFY(!colors.active());
    }
    void companionSeparatesWidgetOwnersAndClosesWhenDisconnected() {
        portal.hold = true;
        QTemporaryDir config;
        QProcess engine;
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("XDG_CONFIG_HOME", config.path());
        engine.setProcessEnvironment(env);
        engine.start(ENGINE_BINARY, {"--serve"});
        QVERIFY(engine.waitForStarted());
        auto cleanup = qScopeGuard([&] {
            engine.terminate();
            engine.waitForFinished(3000);
        });
        const QString service = "io.github.brianidavide.Kalipso.Engine";
        QTRY_VERIFY(QDBusConnection::sessionBus().interface()->isServiceRegistered(service).value());
        auto call = [&](const QDBusConnection &bus, const QString &method, const QString &token) {
            auto message =
                QDBusMessage::createMethodCall(service, "/io/github/brianidavide/Kalipso/Engine", service, method);
            message.setArguments({token});
            return bus.call(message);
        };
        QString session;
        {
            const auto owner = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "color-owner");
            QCOMPARE(call(owner, "StartColors", "widget-a").arguments().first().toBool(), true);
            QTRY_VERIFY(portal.calls.contains("CreateSession"));
            session = portal.session;
            QCOMPARE(call(owner, "StartColors", "widget-b").errorName(),
                     QString("org.freedesktop.DBus.Error.AccessDenied"));
            QCOMPARE(call(owner, "StopColors", "widget-b").errorName(),
                     QString("org.freedesktop.DBus.Error.AccessDenied"));
            QCOMPARE(call(QDBusConnection::sessionBus(), "GetColorState", "widget-a").errorName(),
                     QString("org.freedesktop.DBus.Error.AccessDenied"));
            QCOMPARE(call(owner, "GetColorState", "widget-a").type(), QDBusMessage::ReplyMessage);
        }
        QDBusConnection::disconnectFromBus("color-owner");
        QTRY_VERIFY(portal.closed.contains(session));
        QCOMPARE(call(QDBusConnection::sessionBus(), "StartColors", "widget-c").arguments().first().toBool(), true);
        call(QDBusConnection::sessionBus(), "StopColors", "widget-c");
    }
    void realPipeWireFramesAreSampledAndReleased() {
        if (!qEnvironmentVariableIsSet("KALIPSO_PIPEWIRE_TEST"))
            QSKIP("Run through the isolated PipeWire stream harness.");
        portal.streamNode = qEnvironmentVariableIntValue("KALIPSO_TEST_VIDEO_NODE");
        QVERIFY(portal.streamNode);
        PortalColors colors;
        QSignalSpy samples(&colors, &PortalColors::colorChanged);
        colors.start();
        QTRY_VERIFY_WITH_TIMEOUT(!samples.isEmpty() || !colors.active(), 12000);
        QVERIFY2(!samples.isEmpty(), qPrintable(colors.status()));
        QCOMPARE(samples.last()[0].value<QColor>(), QColor(qEnvironmentVariable("KALIPSO_TEST_VIDEO_COLOR")));
        QVERIFY(colors.active());
        QTest::qWait(4500);
        QVERIFY(samples.size() >= 2);
        QVERIFY(samples.size() <= 4);
        colors.stop();
        const auto count = samples.size();
        QTest::qWait(2500);
        QCOMPARE(samples.size(), count);
        QTRY_VERIFY(portal.closed.contains(portal.session));
        QProcess dump;
        dump.start("pw-dump");
        QTRY_COMPARE_WITH_TIMEOUT(dump.state(), QProcess::NotRunning, 5000);
        QCOMPARE(dump.exitCode(), 0);
        const auto objects = QJsonDocument::fromJson(dump.readAllStandardOutput()).array();
        for (const auto &object : objects)
            QVERIFY(
                object.toObject().value("info").toObject().value("props").toObject().value("media.name").toString() !=
                "Kalipso window colors");
    }
    void stoppingDuringPermissionClosesRequestAndSession() {
        portal.hold = true;
        PortalColors colors;
        colors.start();
        QTRY_VERIFY(!portal.request.isEmpty() && portal.calls.contains("CreateSession"));
        const auto request = portal.request, session = portal.session;
        colors.stop();
        QTRY_VERIFY(portal.closed.contains(request));
        QTRY_VERIFY(portal.closed.contains(session));
        portal.respond(request, 0, {{"session_handle", session}});
        QTest::qWait(50);
        QVERIFY(!colors.active());
        QVERIFY(!portal.calls.contains("SelectSources"));
    }
    void oldResponseCannotStartANewSession() {
        portal.hold = true;
        PortalColors colors;
        colors.start();
        QTRY_VERIFY(portal.calls.contains("CreateSession"));
        const auto request = portal.request, session = portal.session;
        colors.start();
        QTRY_COMPARE(portal.calls.count("CreateSession"), 2);
        portal.respond(request, 0, {{"session_handle", session}});
        QTest::qWait(50);
        QVERIFY(!portal.calls.contains("SelectSources"));
        colors.stop();
    }
    void revocationStopsAndKeepsTheFallback() {
        portal.hold = true;
        PortalColors colors;
        QSignalSpy samples(&colors, &PortalColors::colorChanged);
        colors.start();
        QTRY_VERIFY(portal.calls.contains("CreateSession"));
        auto signal = QDBusMessage::createSignal(portal.session, "org.freedesktop.portal.Session", "Closed");
        signal.setArguments({QVariantMap{}});
        QDBusConnection::sessionBus().send(signal);
        QTRY_VERIFY(!colors.active());
        QVERIFY(colors.status().contains("ended"));
        QVERIFY(samples.isEmpty());
    }
};
QTEST_GUILESS_MAIN(PortalTests)
#include "portal_colors_tests.moc"
