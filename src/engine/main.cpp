// SPDX-License-Identifier: AGPL-3.0-or-later
// Optional session D-Bus companion: local speech recognition and window-color
// sampling for the Plasma widget and GNOME extension.
#include "config.h"
#include "screen_colors/portal.h"
#include "settings_migration.h"
#include "speech.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QSocketNotifier>
#include <QTimer>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

namespace {
constexpr auto kService = "io.github.brianidavide.Kalipso.Engine";
constexpr auto kObjectPath = "/io/github/brianidavide/Kalipso/Engine";
constexpr qint64 kLeaseMs = 10000; // Clients poll; a silent client loses its session.
constexpr qint64 kIdleExitMs = 90000;
constexpr int kMaxEvents = 64;

int shutdownWriteFd = -1;
void requestShutdown(int) {
    const int saved = errno;
    const char byte = 1;
    if (shutdownWriteFd >= 0) {
        [[maybe_unused]] const auto written = ::write(shutdownWriteFd, &byte, 1);
    }
    errno = saved;
}

QString json(const QJsonObject &value) {
    return QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact));
}
} // namespace

class Engine final : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.brianidavide.Kalipso.Engine")
public:
    Engine() {
        speechWatcher_.setConnection(QDBusConnection::sessionBus());
        speechWatcher_.setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
        colorWatcher_.setConnection(QDBusConnection::sessionBus());
        colorWatcher_.setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
        connect(&speechWatcher_, &QDBusServiceWatcher::serviceUnregistered, this, [this](const QString &name) {
            if (name == owner_)
                stopSpeech();
        });
        connect(&colorWatcher_, &QDBusServiceWatcher::serviceUnregistered, this, [this](const QString &name) {
            if (name == colorOwner_)
                stopColors();
        });
        connect(&colors_, &PortalColors::colorChanged, this, [this](const QColor &color) { color_ = color.name(); });

        state_["backend"] = SpeechConfig::selected();
        connect(&speech_, &SpeechService::listening, this, [this] {
            state_["status"] = "listening";
            publish();
        });
        connect(&speech_, &SpeechService::status, this, [this](const QString &message) {
            state_["message"] = message;
            if (!speech_.active()) {
                state_["status"] = "error";
                releaseOwner();
            }
            publish();
        });
        connect(&speech_, &SpeechService::transcript, this, [this](const QString &text, bool final) {
            state_["text"] = text;
            state_["final"] = final;
            state_["sequence"] = ++sequence_;
            events_.append(QJsonObject{{"sequence", sequence_}, {"text", text}, {"final", final}});
            while (events_.size() > kMaxEvents)
                events_.removeFirst();
            state_["events"] = events_;
            publish();
        });

        idle_.start();
        connect(&leaseTimer_, &QTimer::timeout, this, [this] {
            if (!owner_.isEmpty() && lease_.elapsed() > kLeaseMs)
                stopSpeech();
            if (!colorOwner_.isEmpty() && colorLease_.elapsed() > kLeaseMs)
                stopColors();
            if (owner_.isEmpty() && colorOwner_.isEmpty() && idle_.elapsed() > kIdleExitMs)
                QCoreApplication::quit();
        });
        leaseTimer_.start(1000);
    }

public slots:
    // --- Window colors (Plasma "Choose background window…") ---
    bool StartColors(const QString &clientToken) {
        if (!calledFromDBus())
            return false;
        if (clientToken.isEmpty() || clientToken.size() > 128) {
            sendErrorReply(QDBusError::InvalidArgs, "A widget identifier is required.");
            return false;
        }
        const auto caller = message().service();
        if (!colorOwner_.isEmpty() && (caller != colorOwner_ || clientToken != colorToken_)) {
            sendErrorReply(QDBusError::AccessDenied, "Another widget is sharing a background window.");
            return false;
        }
        colorOwner_ = caller;
        colorToken_ = clientToken;
        colorWatcher_.addWatchedService(caller);
        colorLease_.start();
        idle_.restart();
        color_.clear();
        colors_.start();
        return true;
    }

    QString GetColorState(const QString &clientToken) {
        if (!ownsColors(clientToken)) {
            sendErrorReply(QDBusError::AccessDenied, "Only the sharing widget can read its color state.");
            return {};
        }
        colorLease_.restart();
        idle_.restart();
        return json({{"active", colors_.active()}, {"message", colors_.status()}, {"color", color_}});
    }

    void StopColors(const QString &clientToken) {
        if (!ownsColors(clientToken)) {
            sendErrorReply(QDBusError::AccessDenied, "Only the sharing widget can stop its session.");
            return;
        }
        stopColors();
    }

    // --- Speech ---
    QString GetDependencies() {
        idle_.restart();
        return json(SpeechConfig::dependencies(SpeechConfig::selected()));
    }

    QString GetBackends() {
        idle_.restart();
        return json({{"selected", SpeechConfig::selected()}, {"backends", SpeechConfig::backends()}});
    }

    bool SelectBackend(const QString &backend) {
        if (!calledFromDBus())
            return false;
        if (speech_.active() || !owner_.isEmpty()) {
            sendErrorReply(QDBusError::AccessDenied, "Stop listening before changing the speech engine.");
            return false;
        }
        if (!SpeechConfig::supported(backend)) {
            sendErrorReply(QDBusError::InvalidArgs, "Choose Moonshine or Vosk.");
            return false;
        }
        QSettings config;
        config.setValue("speech/backend", backend);
        config.sync();
        if (config.status() != QSettings::NoError) {
            sendErrorReply(QDBusError::Failed, "Cannot save the speech engine selection.");
            return false;
        }
        resetTranscript();
        state_["backend"] = backend;
        state_["status"] = "ready";
        state_["message"] = "Speech engine selected. Start listening when its model is configured.";
        idle_.restart();
        publish();
        return true;
    }

    QString GetState() {
        idle_.restart();
        if (calledFromDBus() && message().service() == owner_)
            lease_.restart();
        return json(state_);
    }

    bool Start() {
        if (!calledFromDBus())
            return false;
        const auto caller = message().service();
        if (!owner_.isEmpty()) {
            if (caller == owner_) {
                lease_.restart();
                return true;
            }
            sendErrorReply(QDBusError::AccessDenied,
                           "Another frontend owns the microphone. Stop listening there first.");
            return false;
        }
        const auto check = SpeechConfig::dependencies(SpeechConfig::selected());
        if (!check.value("ready").toBool()) {
            state_["status"] = "missing-dependencies";
            state_["message"] = check.value("missing").toArray().first().toString();
            publish();
            return false;
        }
        owner_ = caller;
        speechWatcher_.addWatchedService(owner_);
        lease_.start();
        resetTranscript();
        state_["status"] = "loading";
        state_["message"] = "Loading local speech model";
        state_["backend"] = check.value("backend");
        publish();

        SpeechRequest request;
        request.backend = check.value("backend").toString();
        request.library = check.value("library").toString();
        request.model = check.value("model").toString();
        request.language = check.value("language").toString();
        request.architecture = check.value("architecture").toInt();
        request.microphone = QSettings().value("speech/microphone").toString();
        speech_.start(request);
        return speech_.active();
    }

    void Stop() {
        if (calledFromDBus() && !owner_.isEmpty() && message().service() != owner_) {
            sendErrorReply(QDBusError::AccessDenied, "Only the listening frontend can stop its session.");
            return;
        }
        stopSpeech();
    }

signals:
    void StateChanged(const QString &state);

private:
    bool ownsColors(const QString &token) const {
        return calledFromDBus() &&
               (colorOwner_.isEmpty() || (message().service() == colorOwner_ && token == colorToken_));
    }
    void publish() {
        state_["active"] = speech_.active();
        emit StateChanged(json(state_));
    }
    void resetTranscript() {
        events_ = {};
        state_["events"] = events_;
        state_["text"] = "";
        state_["final"] = false;
        state_["sequence"] = ++sequence_;
    }
    void releaseOwner() {
        if (!owner_.isEmpty())
            speechWatcher_.removeWatchedService(owner_);
        owner_.clear();
        idle_.restart();
    }
    void stopSpeech() {
        speech_.stop();
        releaseOwner();
        state_["status"] = "stopped";
        state_["message"] = "Microphone stopped";
        publish();
    }
    void stopColors() {
        colors_.stop();
        colorWatcher_.removeWatchedService(colorOwner_);
        colorOwner_.clear();
        colorToken_.clear();
        color_.clear();
        idle_.restart();
    }

    SpeechService speech_;
    QDBusServiceWatcher speechWatcher_;
    QString owner_;
    QElapsedTimer lease_;
    QJsonArray events_;
    int sequence_ = 0;
    QJsonObject state_{
        {"status", "ready"}, {"message", "Local engine ready"}, {"active", false}, {"text", ""}, {"final", false},
        {"sequence", 0},     {"backend", "moonshine"}};

    PortalColors colors_;
    QDBusServiceWatcher colorWatcher_;
    QString colorOwner_, colorToken_, color_;
    QElapsedTimer colorLease_;

    QTimer leaseTimer_;
    QElapsedTimer idle_;
};

namespace {
// Updates the saved profile from command-line options. Returns a process exit
// code, or -1 to continue.
int configure(const QCommandLineParser &args) {
    const auto backend = args.isSet("backend") ? args.value("backend") : SpeechConfig::selected();
    bool architectureOk = true;
    int architecture = 2;
    if (args.isSet("architecture")) {
        architecture = args.value("architecture").toInt(&architectureOk);
        architectureOk = architectureOk && architecture >= 2 && architecture <= 5;
    }
    if (!SpeechConfig::supported(backend) || !architectureOk) {
        std::fprintf(stderr, "Choose --backend moonshine|vosk and a streaming --architecture 2|3|4|5.\n");
        return 2;
    }
    QDBusInterface running(kService, kObjectPath, kService);
    if (running.isValid()) {
        const QDBusReply<QString> reply = running.call("GetState");
        if (reply.isValid() && QJsonDocument::fromJson(reply.value().toUtf8()).object().value("active").toBool()) {
            std::fprintf(stderr, "Stop listening before configuring a speech engine.\n");
            return 2;
        }
    }
    QSettings config;
    config.setValue("speech/backend", backend);
    for (const auto *name : {"library", "model", "language"})
        if (args.isSet(name))
            config.setValue("profiles/" + backend + "/" + name, args.value(name));
    if (args.isSet("architecture"))
        config.setValue("profiles/" + backend + "/architecture", architecture);
    if (args.isSet("microphone"))
        config.setValue("speech/microphone", args.value("microphone"));
    config.sync();
    if (config.status() != QSettings::NoError) {
        std::fprintf(stderr, "Cannot write configuration\n");
        return 1;
    }
    return -1;
}

void print(const QJsonObject &value) { std::printf("%s\n", json(value).toUtf8().constData()); }
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Kalipso");
    QCoreApplication::setApplicationName("engine");
    importLegacySettings("Teleprompter", "engine");

    QCommandLineParser args;
    args.setApplicationDescription("Optional local speech companion for the Plasma and GNOME frontends.");
    args.addHelpOption();
    args.addOptions({
        {"serve", "Run the session D-Bus service."},
        {"check", "Report missing speech components as JSON."},
        {"configure", "Save the given options, then report as with --check."},
        {"list-backends", "List speech engines and their saved profiles as JSON."},
        {"backend", "Speech engine: moonshine or vosk.", "name"},
        {"library", "Native library path or name.", "path"},
        {"model", "Extracted model folder.", "path"},
        {"language", "Model language, for example en or it.", "code"},
        {"architecture", "Moonshine streaming architecture: 2 tiny, 3 base, 4 small, 5 medium.", "number"},
        {"microphone", "PipeWire source node name; empty for the default.", "node"},
    });
    args.process(app);

    if (args.isSet("configure")) {
        if (const int code = configure(args); code >= 0)
            return code;
    }
    if (args.isSet("list-backends")) {
        print({{"selected", SpeechConfig::selected()}, {"backends", SpeechConfig::backends()}});
        return 0;
    }
    if (args.isSet("check") || args.isSet("configure")) {
        const auto result = SpeechConfig::dependencies(SpeechConfig::selected());
        print(result);
        return result.value("ready").toBool() ? 0 : 1;
    }
    if (!args.isSet("serve"))
        args.showHelp(2);

    auto bus = QDBusConnection::sessionBus();
    Engine engine;
    if (!bus.registerObject(kObjectPath, &engine,
                            QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals) ||
        !bus.registerService(kService)) {
        std::fprintf(stderr, "Cannot register session D-Bus service: %s\n", qPrintable(bus.lastError().message()));
        return 1;
    }

    // Quit cleanly on SIGTERM/SIGINT so the microphone and portal sessions are released.
    int shutdownPipe[2];
    if (::pipe2(shutdownPipe, O_NONBLOCK | O_CLOEXEC) != 0) {
        std::perror("shutdown pipe");
        return 1;
    }
    shutdownWriteFd = shutdownPipe[1];
    std::signal(SIGTERM, requestShutdown);
    std::signal(SIGINT, requestShutdown);
    QSocketNotifier notifier(shutdownPipe[0], QSocketNotifier::Read);
    QObject::connect(&notifier, &QSocketNotifier::activated, &app, [&] {
        char bytes[32];
        [[maybe_unused]] const auto read = ::read(shutdownPipe[0], bytes, sizeof(bytes));
        app.quit();
    });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &engine, &Engine::Stop);
    const int result = app.exec();
    shutdownWriteFd = -1;
    ::close(shutdownPipe[0]);
    ::close(shutdownPipe[1]);
    return result;
}

#include "main.moc"
