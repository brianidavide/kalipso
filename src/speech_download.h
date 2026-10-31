// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "speech_assets.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <atomic>
#include <memory>

class QNetworkReply;
class QThread;

// The speech models Kalipso downloads by itself, from the catalog built into the program
// (tools/data/speech-catalog.json): HTTPS sources pinned by SHA-256. Everything goes under
// <data>/kalipso, where installedSpeech() finds it. Nothing is used before its fingerprint
// matches, and only the runtime's native files are kept.
namespace SpeechCatalog {
const QJsonObject &builtIn();
// The catalog's runtimes are builds for Linux on x86_64.
bool downloadsSupported();
// Where downloads are installed: <writable data>/kalipso.
QString writableRoot();
// Language codes with a model in the catalog, in code order: de, en, es, it, ja, zh.
QStringList languages(const QJsonObject &catalog = builtIn());
// The language's own name, as its speakers write it ("Italiano").
QString languageName(const QString &code);
// The engines with a model for the language, Moonshine first: it follows speech more closely.
QStringList backendsFor(const QString &code, const QJsonObject &catalog = builtIn());
// An installed model for the language: with the preferred engine if it has one, then Moonshine,
// then Vosk. An empty backend when none is installed.
InstalledSpeech installedFor(const QString &code, const QString &preferred = {});
// The engine to download for the language: the preferred one if the catalog has the language for
// it, otherwise the first of backendsFor().
QString downloadBackend(const QString &code, const QString &preferred = {}, const QJsonObject &catalog = builtIn());

// What is still missing for one engine and language.
struct Part {
    enum Kind { Runtime, Archive, File } kind;
    QUrl url;
    QByteArray sha256; // Lowercase hex.
    qint64 bytes = 0;
    QString name; // File: the model file's name.
};
struct Plan {
    bool valid = false;
    QString runtimeDir, library, modelDir, modelName;
    QJsonObject runtime, model;
    QList<Part> parts;
    qint64 bytes() const;
};
Plan plan(const QString &backend, const QString &code, const QString &root, const QJsonObject &catalog = builtIn());
} // namespace SpeechCatalog

// Downloads one engine's runtime (when missing) and one language model, then reports where they
// are. One download at a time; cancel() stops it and removes what was not finished.
class SpeechDownload : public QObject {
    Q_OBJECT
public:
    enum class Error { None, Network, Fingerprint, Archive, Disk, Unsupported, Cancelled };
    Q_ENUM(Error)
    static QString errorName(Error error);

    explicit SpeechDownload(QObject *parent = nullptr);
    ~SpeechDownload() override;

    // Results arrive as signals, also when nothing is missing or the language has no model.
    void start(const QString &backend, const QString &language);
    void cancel();
    bool active() const { return active_; }
    bool unpacking() const { return thread_ != nullptr; }
    QString backend() const { return backend_; }
    QString language() const { return language_; }
    qint64 received() const { return done_ + current_; }
    qint64 total() const { return total_; }

    // For tests: another catalog and folder, and plain HTTP from this computer.
    void setCatalog(const QJsonObject &catalog) { catalog_ = catalog; }
    void setRoot(const QString &root) { root_ = root; }
    void allowLocalHttp() { localHttp_ = true; }

signals:
    void progress(qint64 received, qint64 total);
    void unpackingStarted();
    void finished(const InstalledSpeech &installed);
    void failed(SpeechDownload::Error error, const QString &detail);

private:
    QString root() const { return root_.isEmpty() ? SpeechCatalog::writableRoot() : root_; }
    QString partPath() const;
    bool secure(const QUrl &url) const;
    void begin();
    void next();
    bool take(const QByteArray &chunk);
    void fetched();
    void unpack(const QString &verified);
    void complete();
    void fail(Error error, const QString &detail = {});

    QNetworkAccessManager network_;
    QJsonObject catalog_ = SpeechCatalog::builtIn();
    QString root_, backend_, language_;
    bool localHttp_ = false, active_ = false, tooLarge_ = false, writeFailed_ = false;
    SpeechCatalog::Plan plan_;
    int index_ = 0;
    qint64 done_ = 0, current_ = 0, total_ = 0, reported_ = 0;
    QFile file_;
    QCryptographicHash hash_{QCryptographicHash::Sha256};
    QPointer<QNetworkReply> reply_;
    QThread *thread_ = nullptr;
    std::shared_ptr<std::atomic<bool>> cancelled_ = std::make_shared<std::atomic<bool>>(false);
};
