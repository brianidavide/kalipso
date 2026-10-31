// SPDX-License-Identifier: AGPL-3.0-or-later
// Optional session D-Bus companion: local speech recognition, speech model downloads and
// window-color sampling for the Plasma widget and GNOME extension.
#include "config.h"
#include "screen_colors/portal.h"
#include "settings_migration.h"
#include "speech.h"
#include "speech_download.h"
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

// Makes an installed model the one the companion listens with.
bool useSpeech(const InstalledSpeech &speech) {
    QSettings config;
    config.setValue("speech/backend", speech.backend);
    const QString prefix = "profiles/" + speech.backend + "/";
    config.setValue(prefix + "library", speech.library);
    config.setValue(prefix + "model", speech.model);
    config.setValue(prefix + "language", speech.language);
    if (speech.backend == QLatin1String("moonshine"))
        config.setValue(prefix + "architecture", 2); // The catalog has Tiny streaming models.
    config.sync();
    return config.status() == QSettings::NoError;
}

// Every catalog language: whether it is ready, and with which engine (the one that follows the
// language best, unless only another one is installed).
QJsonArray languageChoices() {
    const auto root = SpeechCatalog::writableRoot();
    QJsonArray result;
    for (const auto &code : SpeechCatalog::languages()) {
        const auto installed = SpeechCatalog::installedFor(code);
        const bool ready = !installed.backend.isEmpty();
        const auto backend = ready ? installed.backend : SpeechCatalog::downloadBackend(code);
        result.append(QJsonObject{{"code", code},
                                  {"name", SpeechCatalog::languageName(code)},
                                  {"backend", backend},
                                  {"ready", ready},
                                  {"bytes", ready ? 0 : SpeechCatalog::plan(backend, code, root).bytes()}});
    }
    return result;
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

        connect(&download_, &SpeechDownload::progress, this, [this](qint64 received, qint64 total) {
            install_["received"] = received;
            install_["total"] = total;
        });
        connect(&download_, &SpeechDownload::finished, this, [this](const InstalledSpeech &speech) {
            if (speech_.active() || !owner_.isEmpty() || !useSpeech(speech)) {
                finishInstall("failed", "busy");
                return;
            }
            install_["backend"] = speech.backend;
            selected(speech.backend);
            finishInstall("done");
        });
        connect(&download_, &SpeechDownload::failed, this, [this](SpeechDownload::Error error, const QString &) {
            finishInstall(error == SpeechDownload::Error::Cancelled ? "cancelled" : "failed",
                          SpeechDownload::errorName(error));
        });

        idle_.start();
        connect(&leaseTimer_, &QTimer::timeout, this, [this] {
            if (!owner_.isEmpty() && lease_.elapsed() > kLeaseMs)
                stopSpeech();
            if (!colorOwner_.isEmpty() && colorLease_.elapsed() > kLeaseMs)
                stopColors();
            if (download_.active())
                idle_.restart(); // A download finishes even if every frontend closed.
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

    // --- Language models ---
    // The catalog languages with their state, and the download in progress (stage: idle,
    // downloading, unpacking, done, failed or cancelled; error: network, fingerprint, archive,
    // disk, unsupported or busy). Clients poll it while a download runs.
    QString GetLanguages() {
        idle_.restart();
        const auto backend = SpeechConfig::selected();
        auto install = install_;
        install["active"] = download_.active();
        if (download_.active())
            install["stage"] = download_.unpacking() ? "unpacking" : "downloading";
        return json({{"downloads", SpeechCatalog::downloadsSupported()},
                     {"backend", backend},
                     {"language", SpeechConfig::value(backend, "language").toString()},
                     {"languages", languageChoices()},
                     {"install", install}});
    }

    // Listens in this language from now on: at once if its model is installed, otherwise after
    // downloading it (and the engine's runtime, if missing). False with an error if not possible.
    bool InstallLanguage(const QString &code) {
        if (!calledFromDBus())
            return false;
        idle_.restart();
        if (speech_.active() || !owner_.isEmpty()) {
            sendErrorReply(QDBusError::AccessDenied, "Stop listening before changing the language.");
            return false;
        }
        if (!SpeechCatalog::languages().contains(code)) {
            sendErrorReply(QDBusError::InvalidArgs, "Kalipso has no speech model for this language.");
            return false;
        }
        if (download_.active()) {
            if (code == download_.language())
                return true;
            sendErrorReply(QDBusError::AccessDenied, "Another language is downloading. Cancel it first.");
            return false;
        }
        install_ = {{"language", code}, {"received", 0}, {"total", 0}, {"error", ""}};
        const auto installed = SpeechCatalog::installedFor(code);
        if (!installed.backend.isEmpty()) {
            if (!useSpeech(installed)) {
                sendErrorReply(QDBusError::Failed, "Cannot save the language.");
                return false;
            }
            install_["backend"] = installed.backend;
            selected(installed.backend);
            finishInstall("done");
            return true;
        }
        const auto backend = SpeechCatalog::downloadBackend(code);
        install_["backend"] = backend;
        install_["stage"] = "downloading";
        install_["total"] = SpeechCatalog::plan(backend, code, SpeechCatalog::writableRoot()).bytes();
        download_.start(backend, code);
        return true;
    }

    void CancelInstall() {
        idle_.restart();
        download_.cancel();
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
    void selected(const QString &backend) {
        resetTranscript();
        state_["backend"] = backend;
        state_["status"] = "ready";
        state_["message"] = "Speech language selected.";
        publish();
    }
    void finishInstall(const QString &stage, const QString &error = {}) {
        install_["stage"] = stage;
        install_["error"] = error;
        idle_.restart();
    }
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

    SpeechDownload download_;
    QJsonObject install_{{"stage", "idle"}, {"language", ""}, {"backend", ""},
                         {"received", 0},   {"total", 0},     {"error", ""}};

    QTimer leaseTimer_;
    QElapsedTimer idle_;
};

namespace {
// True when the running companion has the microphone open.
bool listening() {
    QDBusInterface running(kService, kObjectPath, kService);
    if (!running.isValid())
        return false;
    const QDBusReply<QString> reply = running.call("GetState");
    return reply.isValid() && QJsonDocument::fromJson(reply.value().toUtf8()).object().value("active").toBool();
}

int listModels() {
    for (const auto &value : languageChoices()) {
        const auto item = value.toObject();
        const auto state = item.value("ready").toBool()
                               ? QStringLiteral("installed")
                               : QString("%1 MB to download").arg((item.value("bytes").toInteger() + 524288) >> 20);
        std::printf("%s  %s  %s · %s\n", qPrintable(item.value("code").toString()),
                    qPrintable(item.value("name").toString()), qPrintable(item.value("backend").toString()),
                    qPrintable(state));
    }
    if (!SpeechCatalog::downloadsSupported())
        std::printf("Kalipso downloads speech models on Linux for x86_64 computers only.\n");
    return 0;
}

// Downloads the model for --language (and the engine's runtime, if missing), then listens with it.
int download(const QCommandLineParser &args, QCoreApplication &app) {
    const auto code = args.value("language");
    const auto languages = SpeechCatalog::languages();
    if (!languages.contains(code)) {
        std::fprintf(stderr, "Choose --language %s.\n", qPrintable(languages.join('|')));
        return 2;
    }
    if (args.isSet("model") || args.isSet("library")) {
        std::fprintf(stderr, "--download installs a catalog model; use --configure for --model and --library.\n");
        return 2;
    }
    const auto backend = args.value("backend");
    if (!backend.isEmpty() && !SpeechCatalog::backendsFor(code).contains(backend)) {
        std::fprintf(stderr, "No %s model for %s: choose --backend %s.\n", qPrintable(backend), qPrintable(code),
                     qPrintable(SpeechCatalog::backendsFor(code).join('|')));
        return 2;
    }
    if (listening()) {
        std::fprintf(stderr, "Stop listening before changing the language.\n");
        return 2;
    }
    auto installed = SpeechCatalog::installedFor(code, backend);
    if (!backend.isEmpty() && installed.backend != backend)
        installed = {};
    if (installed.backend.isEmpty()) {
        SpeechDownload download;
        int result = 0, tenth = -1;
        QObject::connect(&download, &SpeechDownload::progress, &app, [&](qint64 received, qint64 total) {
            const int now = total > 0 ? int(received * 10 / total) : 10;
            if (now != tenth) {
                tenth = now;
                std::printf("Downloading %s: %d%% of %lld MB\n", qPrintable(SpeechCatalog::languageName(code)),
                            now * 10, (total + 524288) >> 20);
                std::fflush(stdout);
            }
        });
        QObject::connect(&download, &SpeechDownload::unpackingStarted, &app, [] {
            std::printf("Unpacking and checking\n");
            std::fflush(stdout);
        });
        QObject::connect(&download, &SpeechDownload::finished, &app, [&](const InstalledSpeech &speech) {
            installed = speech;
            app.quit();
        });
        QObject::connect(&download, &SpeechDownload::failed, &app,
                         [&](SpeechDownload::Error error, const QString &detail) {
                             std::fprintf(stderr, "Download failed (%s): %s\n",
                                          qPrintable(SpeechDownload::errorName(error)), qPrintable(detail));
                             result = 1;
                             app.quit();
                         });
        download.start(SpeechCatalog::downloadBackend(code, backend), code);
        app.exec();
        if (result)
            return result;
    }
    if (!useSpeech(installed)) {
        std::fprintf(stderr, "Cannot write configuration\n");
        return 1;
    }
    std::printf("%s is ready, with %s (%s). No microphone was opened.\n"
                "In Kalipso, the Plasma widget or the GNOME extension choose Follow my voice, then Start "
                "listening.\n",
                qPrintable(SpeechCatalog::languageName(code)),
                installed.backend == QLatin1String("moonshine") ? "Moonshine" : "Vosk", qPrintable(installed.model));
    return 0;
}

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
    if (listening()) {
        std::fprintf(stderr, "Stop listening before configuring a speech engine.\n");
        return 2;
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
    args.setApplicationDescription("Local speech companion for the Plasma widget and GNOME extension.");
    args.addHelpOption();
    args.addOptions({
        {"serve", "Run the session D-Bus service."},
        {"check", "Report missing speech components as JSON."},
        {"configure", "Save the given options, then report as with --check."},
        {"list-backends", "List speech engines and their saved profiles as JSON."},
        {"download", "Download the speech model for --language, then listen with it."},
        {"list-models", "List the languages Kalipso can download, and which are installed."},
        {"backend", "Speech engine: moonshine or vosk.", "name"},
        {"library", "Native library path or name.", "path"},
        {"model", "Extracted model folder.", "path"},
        {"language", "Model language, for example en or it.", "code"},
        {"architecture", "Moonshine streaming architecture: 2 tiny, 3 base, 4 small, 5 medium.", "number"},
        {"microphone", "PipeWire source node name; empty for the default.", "node"},
    });
    args.process(app);

    if (args.isSet("list-models"))
        return listModels();
    if (args.isSet("download"))
        return download(args, app);
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
