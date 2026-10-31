// SPDX-License-Identifier: AGPL-3.0-or-later
#include "speech_download.h"
#include "zip.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QStorageInfo>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QThread>
#include <utility>

namespace {
constexpr qint64 kMaxMember = 512LL * 1024 * 1024;    // One unpacked file.
constexpr qint64 kMaxUnpacked = 1024LL * 1024 * 1024; // One archive, against decompression bombs.
constexpr qint64 kReportEvery = 256 * 1024;

// One path segment: no folders, no climbing out.
bool plainName(const QString &name) {
    return !name.isEmpty() && name != QLatin1String(".") && name != QLatin1String("..") && !name.contains(u'/') &&
           !name.contains(u'\\');
}

// A member path that stays inside the folder it is unpacked to.
bool safeMember(const QString &name) {
    if (name.isEmpty() || name.startsWith(u'/') || name.contains(u'\\') || name.contains(u':'))
        return false;
    for (const auto &part : name.split(u'/'))
        if (part == QLatin1String(".."))
            return false;
    return true;
}

bool writeFile(const QString &path, const QByteArray &bytes) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size() && file.flush();
}

struct Unpack {
    QString archive, parent, target;
    QString prefix;                   // Members under this folder, without it (a model archive)...
    QStringList only;                 // ...or exactly these members, with their paths (a runtime's native files).
    QHash<QString, QByteArray> extra; // Files added next to them.
};

struct Unpacked {
    SpeechDownload::Error error = SpeechDownload::Error::None;
    QString detail;
};

// Runs on a worker thread: reads the verified archive, refuses unsafe members before writing
// anything, unpacks into a hidden folder and moves it into place in one step.
Unpacked unpackArchive(const Unpack &job, const std::atomic<bool> &cancelled) {
    using Error = SpeechDownload::Error;
    QFile file(job.archive);
    if (!file.open(QIODevice::ReadOnly))
        return {Error::Disk, "Cannot read " + job.archive};
    const QByteArray data = file.readAll();
    file.close();
    const ZipArchive zip(data, {kMaxMember, kMaxUnpacked});
    if (!zip.valid())
        return {Error::Archive, "The download is not a ZIP archive."};
    const auto names = zip.names();
    for (const auto &name : names)
        if (!safeMember(name) || zip.isLink(name))
            return {Error::Archive, "Unsafe archive member: " + name};
    for (const auto &name : job.only)
        if (!zip.contains(name))
            return {Error::Archive, "The archive has no " + name};
    const QString destination = job.parent + "/" + job.target;
    if (QFileInfo::exists(destination))
        return {}; // Installed meanwhile, by another Kalipso window: complete() checks it.
    if (!QDir().mkpath(job.parent))
        return {Error::Disk, "Cannot create " + job.parent};
    QTemporaryDir staging(job.parent + "/.install-XXXXXX");
    if (!staging.isValid())
        return {Error::Disk, "Cannot write in " + job.parent};
    const QString out = staging.path() + "/" + job.target;
    for (const auto &name : names) {
        if (cancelled)
            return {Error::Cancelled, {}};
        if (name.endsWith(u'/'))
            continue;
        QString relative;
        if (!job.only.isEmpty()) {
            if (!job.only.contains(name))
                continue;
            relative = name;
        } else {
            if (!name.startsWith(job.prefix))
                continue;
            relative = name.mid(job.prefix.size());
        }
        bool ok = false;
        const auto bytes = zip.file(name, &ok);
        if (!ok)
            return {Error::Archive, "Cannot unpack " + name};
        if (!writeFile(out + "/" + relative, bytes))
            return {Error::Disk, "Cannot write " + relative};
    }
    for (auto it = job.extra.cbegin(); it != job.extra.cend(); ++it)
        if (!writeFile(out + "/" + it.key(), it.value()))
            return {Error::Disk, "Cannot write " + it.key()};
    if (!QDir().rename(out, destination) && !QFileInfo::exists(destination))
        return {Error::Disk, "Cannot move the files into " + destination};
    return {};
}
} // namespace

namespace SpeechCatalog {
const QJsonObject &builtIn() {
    static const QJsonObject catalog = [] {
        QFile file(":/speech/speech-catalog.json");
        return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
    }();
    return catalog;
}

bool downloadsSupported() {
#ifdef Q_OS_LINUX
    return QSysInfo::currentCpuArchitecture() == QLatin1String("x86_64");
#else
    return false;
#endif
}

QString writableRoot() { return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/kalipso"; }

QStringList languages(const QJsonObject &catalog) {
    QStringList codes;
    for (const QString backend : {"moonshine", "vosk"})
        for (const auto &code : catalog.value(backend).toObject().value("models").toObject().keys())
            if (!codes.contains(code))
                codes.append(code);
    codes.sort();
    return codes;
}

QString languageName(const QString &code) {
    static const QHash<QString, QString> names{{"de", "Deutsch"},  {"en", "English"},  {"es", "Español"},
                                               {"fr", "Français"}, {"it", "Italiano"}, {"ja", "日本語"},
                                               {"zh", "中文"}};
    return names.value(code, code);
}

QStringList backendsFor(const QString &code, const QJsonObject &catalog) {
    QStringList found;
    for (const QString backend : {"moonshine", "vosk"})
        if (catalog.value(backend).toObject().value("models").toObject().contains(code))
            found.append(backend);
    return found;
}

InstalledSpeech installedFor(const QString &code, const QString &preferred) {
    QStringList order{"moonshine", "vosk"};
    if (order.removeAll(preferred))
        order.prepend(preferred);
    const auto installed = installedSpeech();
    for (const auto &backend : order) {
        // A catalog model first, then any model marked with the language.
        const auto name =
            builtIn().value(backend).toObject().value("models").toObject().value(code).toObject().value("name");
        for (const auto &item : installed)
            if (item.backend == backend && QDir(item.model).dirName() == name.toString())
                return item;
        for (const auto &item : installed)
            if (item.backend == backend && item.language == code)
                return item;
    }
    return {};
}

QString downloadBackend(const QString &code, const QString &preferred, const QJsonObject &catalog) {
    const auto backends = backendsFor(code, catalog);
    return backends.contains(preferred) ? preferred : backends.value(0);
}

qint64 Plan::bytes() const {
    qint64 sum = 0;
    for (const auto &part : parts)
        sum += part.bytes;
    return sum;
}

Plan plan(const QString &backend, const QString &code, const QString &root, const QJsonObject &catalog) {
    Plan result;
    const auto runtime = catalog.value(backend).toObject();
    const auto model = runtime.value("models").toObject().value(code).toObject();
    const auto version = runtime.value("version").toString();
    const auto modelName = model.value("name").toString();
    if (model.isEmpty() || !plainName(backend + "-" + version) || !plainName(modelName) ||
        !safeMember(runtime.value("library").toString()))
        return result;
    result.runtime = runtime;
    result.model = model;
    result.modelName = modelName;
    result.runtimeDir = root + "/runtimes/" + backend + "-" + version;
    result.library = result.runtimeDir + "/" + runtime.value("library").toString();
    result.modelDir = root + "/models/" + backend + "/" + modelName;
    const auto part = [](Part::Kind kind, const QJsonObject &source, const QString &url, const QString &name = {}) {
        return Part{kind, QUrl(url), source.value("sha256").toString().toLatin1().toLower(),
                    source.value("bytes").toInteger(), name};
    };
    if (!QFileInfo(result.library).isFile())
        result.parts.append(part(Part::Runtime, runtime, runtime.value("url").toString()));
    if (backend == QLatin1String("moonshine")) {
        for (const auto &value : model.value("files").toArray()) {
            const auto file = value.toObject();
            const auto name = file.value("name").toString();
            if (!plainName(name))
                return {};
            // Files are moved into place only once verified, so a complete size means a good file.
            const QFileInfo existing(result.modelDir + "/" + name);
            if (!existing.isFile() || existing.size() != file.value("bytes").toInteger())
                result.parts.append(part(Part::File, file, model.value("source").toString() + "/" + name, name));
        }
    } else if (!SpeechConfig::modelComplete(backend, result.modelDir)) {
        result.parts.append(part(Part::Archive, model, model.value("url").toString()));
    }
    result.valid = true;
    return result;
}
} // namespace SpeechCatalog

QString SpeechDownload::errorName(Error error) {
    switch (error) {
    case Error::None: return {};
    case Error::Network: return "network";
    case Error::Fingerprint: return "fingerprint";
    case Error::Archive: return "archive";
    case Error::Disk: return "disk";
    case Error::Unsupported: return "unsupported";
    case Error::Cancelled: return "cancelled";
    }
    return {};
}

SpeechDownload::SpeechDownload(QObject *parent) : QObject(parent) {}

SpeechDownload::~SpeechDownload() {
    cancelled_->store(true);
    if (reply_) {
        reply_->disconnect(this);
        reply_->abort();
    }
    if (thread_) {
        thread_->disconnect(this);
        thread_->wait();
        delete thread_;
    }
    if (file_.isOpen())
        file_.close();
    if (!plan_.parts.isEmpty())
        QFile::remove(partPath());
}

QString SpeechDownload::partPath() const {
    const auto &part = plan_.parts.value(index_);
    return root() + "/.downloads/" + QString::fromLatin1(part.sha256) + ".part";
}

bool SpeechDownload::secure(const QUrl &url) const {
    if (url.scheme() == QLatin1String("https"))
        return true;
    return localHttp_ && url.scheme() == QLatin1String("http") &&
           (url.host() == QLatin1String("127.0.0.1") || url.host() == QLatin1String("localhost"));
}

void SpeechDownload::start(const QString &backend, const QString &language) {
    if (active_)
        return;
    active_ = true;
    backend_ = backend;
    language_ = language;
    cancelled_ = std::make_shared<std::atomic<bool>>(false);
    plan_ = {};
    index_ = 0;
    done_ = current_ = total_ = reported_ = 0;
    QMetaObject::invokeMethod(this, &SpeechDownload::begin, Qt::QueuedConnection);
}

void SpeechDownload::begin() {
    if (*cancelled_)
        return fail(Error::Cancelled);
    if (!localHttp_ && !SpeechCatalog::downloadsSupported())
        return fail(Error::Unsupported, "Kalipso downloads speech models on Linux for x86_64 computers.");
    plan_ = SpeechCatalog::plan(backend_, language_, root(), catalog_);
    if (!plan_.valid)
        return fail(Error::Unsupported, "The catalog has no " + backend_ + " model for " + language_ + ".");
    total_ = plan_.bytes();
    if (!QDir().mkpath(root() + "/.downloads"))
        return fail(Error::Disk, "Cannot create " + root());
    // Room for the download and for what it unpacks to.
    const auto available = QStorageInfo(root()).bytesAvailable();
    if (available >= 0 && available < 2 * total_ + 16 * 1024 * 1024)
        return fail(Error::Disk, QString("%1 MB free, %2 MB needed").arg(available >> 20).arg((2 * total_) >> 20));
    emit progress(0, total_);
    next();
}

void SpeechDownload::next() {
    if (*cancelled_)
        return fail(Error::Cancelled);
    if (index_ >= plan_.parts.size())
        return complete();
    const auto &part = plan_.parts.at(index_);
    if (!secure(part.url))
        return fail(Error::Network, "Only HTTPS sources are allowed: " + part.url.toString());
    file_.setFileName(partPath());
    if (!file_.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return fail(Error::Disk, "Cannot write " + file_.fileName());
    hash_.reset();
    current_ = 0;
    tooLarge_ = writeFailed_ = false;
    QNetworkRequest request(part.url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(60000); // A minute without data.
    const auto proxies =
        localHttp_ ? QList<QNetworkProxy>() : QNetworkProxyFactory::systemProxyForQuery(QNetworkProxyQuery(part.url));
    network_.setProxy(proxies.value(0, QNetworkProxy(QNetworkProxy::NoProxy)));
    reply_ = network_.get(request);
    connect(reply_, &QNetworkReply::readyRead, this, [this] {
        if (reply_)
            take(reply_->readAll());
    });
    connect(reply_, &QNetworkReply::finished, this, &SpeechDownload::fetched);
}

bool SpeechDownload::take(const QByteArray &chunk) {
    if (chunk.isEmpty() || tooLarge_ || writeFailed_)
        return false;
    const auto &part = plan_.parts.at(index_);
    current_ += chunk.size();
    if (part.bytes > 0 && current_ > part.bytes) {
        tooLarge_ = true;
    } else {
        hash_.addData(chunk);
        writeFailed_ = file_.write(chunk) != chunk.size();
    }
    if (tooLarge_ || writeFailed_) {
        if (reply_)
            reply_->abort();
        return false;
    }
    if (received() - reported_ >= kReportEvery) {
        reported_ = received();
        emit progress(received(), total_);
    }
    return true;
}

void SpeechDownload::fetched() {
    QNetworkReply *reply = std::exchange(reply_, nullptr);
    if (!reply)
        return;
    reply->deleteLater();
    if (reply->error() == QNetworkReply::NoError)
        take(reply->readAll());
    file_.close();
    const auto &part = plan_.parts.at(index_);
    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto drop = [this](Error error, const QString &detail) {
        QFile::remove(file_.fileName());
        fail(error, detail);
    };
    if (*cancelled_)
        return drop(Error::Cancelled, {});
    if (writeFailed_)
        return drop(Error::Disk, "Cannot write " + file_.fileName());
    if (tooLarge_)
        return drop(Error::Fingerprint, part.url.toString() + " is larger than expected.");
    if (reply->error() != QNetworkReply::NoError)
        return drop(Error::Network, reply->errorString());
    if (status != 200)
        return drop(Error::Network, QString("HTTP %1 from %2").arg(status).arg(reply->url().toString()));
    if (!secure(reply->url()))
        return drop(Error::Network, "Redirected to an insecure address: " + reply->url().toString());
    if (hash_.result().toHex() != part.sha256)
        return drop(Error::Fingerprint, part.url.toString() + " does not match its fingerprint.");
    done_ += current_;
    current_ = 0;
    const QString verified = root() + "/.downloads/" + QString::fromLatin1(part.sha256);
    QFile::remove(verified);
    if (!QFile::rename(file_.fileName(), verified))
        return drop(Error::Disk, "Cannot keep " + verified);
    if (part.kind != SpeechCatalog::Part::File)
        return unpack(verified);
    if (!QDir().mkpath(plan_.modelDir))
        return fail(Error::Disk, "Cannot create " + plan_.modelDir);
    const QString target = plan_.modelDir + "/" + part.name;
    QFile::remove(target);
    if (!QFile::rename(verified, target)) {
        QFile::remove(verified);
        return fail(Error::Disk, "Cannot write " + target);
    }
    ++index_;
    next();
}

void SpeechDownload::unpack(const QString &verified) {
    const auto &part = plan_.parts.at(index_);
    Unpack job;
    job.archive = verified;
    if (part.kind == SpeechCatalog::Part::Runtime) {
        job.parent = QFileInfo(plan_.runtimeDir).absolutePath();
        job.target = QFileInfo(plan_.runtimeDir).fileName();
        for (const auto &name : plan_.runtime.value("nativeFiles").toArray())
            job.only.append(name.toString());
        // The licences that go with the native libraries, and where they came from.
        QDirIterator notices(":/speech/notices/" + backend_, QDir::Files);
        while (notices.hasNext()) {
            QFile notice(notices.next());
            if (notice.open(QIODevice::ReadOnly))
                job.extra.insert("notices/" + notices.fileName(), notice.readAll());
        }
        auto source = plan_.runtime;
        source.remove("models");
        job.extra.insert("source.json", QJsonDocument(source).toJson());
    } else {
        job.parent = QFileInfo(plan_.modelDir).absolutePath();
        job.target = plan_.modelName;
        job.prefix = plan_.modelName + "/";
    }
    emit unpackingStarted();
    auto result = std::make_shared<Unpacked>();
    const auto cancelled = cancelled_;
    thread_ = QThread::create([job, cancelled, result] { *result = unpackArchive(job, *cancelled); });
    connect(thread_, &QThread::finished, this, [this, result, verified] {
        thread_->deleteLater();
        thread_ = nullptr;
        QFile::remove(verified);
        if (*cancelled_)
            return fail(Error::Cancelled);
        if (result->error != Error::None)
            return fail(result->error, result->detail);
        ++index_;
        next();
    });
    thread_->start();
}

void SpeechDownload::complete() {
    QDir().rmdir(root() + "/.downloads");
    // Records the language, so installedSpeech() can name the model.
    auto source = plan_.model;
    source.remove("files");
    source.insert("language", language_);
    if (QFileInfo(plan_.modelDir).isDir())
        writeFile(plan_.modelDir + "/kalipso-source.json", QJsonDocument(source).toJson());
    if (!QFileInfo(plan_.library).isFile() || !SpeechConfig::modelComplete(backend_, plan_.modelDir))
        return fail(Error::Archive, plan_.modelDir + " is incomplete. Move it aside and choose the language again.");
    active_ = false;
    emit progress(total_, total_);
    emit finished({backend_, plan_.library, plan_.modelDir, language_});
}

void SpeechDownload::cancel() {
    if (!active_)
        return;
    cancelled_->store(true);
    if (reply_)
        reply_->abort(); // finished() follows and reports the cancellation.
    // While unpacking, the worker stops at the next file; otherwise next() stops.
}

void SpeechDownload::fail(Error error, const QString &detail) {
    if (!active_)
        return;
    active_ = false;
    if (file_.isOpen())
        file_.close();
    if (!detail.isEmpty())
        qWarning("Speech download: %s", qPrintable(detail));
    emit failed(error, detail);
}
