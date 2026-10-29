// SPDX-License-Identifier: AGPL-3.0-or-later
#include "impress.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QStandardPaths>

ImpressFollower::ImpressFollower(QObject *parent) : QObject(parent) {
    connect(&helper_, &QProcess::readyReadStandardOutput, this, &ImpressFollower::readLines);
    connect(&helper_, &QProcess::finished, this, [this](int code) {
        const auto errors = QString::fromUtf8(helper_.readAllStandardError());
        if (!stopping_ && code != 0)
            emit statusChanged(errors.contains(QLatin1String("No module named 'uno'"))
                                   ? tr("LibreOffice's Python bridge is missing (on Debian and Ubuntu: python3-uno).")
                                   : tr("Stopped following Impress."));
        stopping_ = false;
        emit finished();
    });
}

ImpressFollower::~ImpressFollower() { stop(); }

ImpressFollower::Office ImpressFollower::findOffice() {
    QStringList candidates{QStandardPaths::findExecutable("soffice"), QStandardPaths::findExecutable("libreoffice")};
#if defined(Q_OS_WIN)
    for (const auto &root : {qEnvironmentVariable("ProgramFiles"), qEnvironmentVariable("ProgramFiles(x86)")})
        candidates << root + "/LibreOffice/program/soffice.exe";
#elif defined(Q_OS_MACOS)
    candidates << "/Applications/LibreOffice.app/Contents/MacOS/soffice";
#endif
    Office office;
    for (const auto &candidate : candidates)
        if (!candidate.isEmpty() && QFileInfo(candidate).isExecutable()) {
            office.soffice = candidate;
            break;
        }
    if (office.soffice.isEmpty())
        return office;
    // LibreOffice's own Python on Windows and macOS (and in builds that bundle it);
    // otherwise a system Python with the uno module (python3-uno on Debian and Ubuntu).
    const QDir program = QFileInfo(QFileInfo(office.soffice).canonicalFilePath()).dir();
    for (const auto &bundled :
         {program.filePath("python.exe"), program.filePath("python"), program.filePath("../Resources/python")})
        if (QFileInfo(bundled).isExecutable()) {
            office.python = QFileInfo(bundled).canonicalFilePath();
            return office;
        }
    office.python = QStandardPaths::findExecutable("python3");
    return office;
}

bool ImpressFollower::start(const QString &presentation) {
    const auto office = findOffice();
    if (office.soffice.isEmpty() || office.python.isEmpty()) {
        emit statusChanged(tr("LibreOffice was not found. Install it to follow an Impress slideshow."));
        return false;
    }
    const auto script = scripts_.filePath("impress_follow.py");
    if (!QFile::exists(script) && !QFile::copy(":/impress/impress_follow.py", script)) {
        emit statusChanged(tr("Cannot prepare the Impress connection."));
        return false;
    }
    // A local named pipe with an unguessable name; LibreOffice opens it only because we ask.
    const auto pipe = QStringLiteral("kalipso-%1").arg(QRandomGenerator::system()->generate64(), 16, 16, QChar('0'));
    if (!QProcess::startDetached(office.soffice, {"--norestore", "--accept=pipe,name=" + pipe + ";urp;",
                                                  QDir::toNativeSeparators(presentation)})) {
        emit statusChanged(tr("LibreOffice could not be started."));
        return false;
    }
    startHelper(office.python, {script, pipe, QFileInfo(presentation).absoluteFilePath()});
    return true;
}

void ImpressFollower::startHelper(const QString &program, const QStringList &arguments) {
    stop();
    pending_.clear();
    helper_.start(program, arguments);
}

void ImpressFollower::stop() {
    if (!active())
        return;
    stopping_ = true;
    helper_.kill();
    helper_.waitForFinished(2000);
}

void ImpressFollower::readLines() {
    pending_ += helper_.readAllStandardOutput();
    for (qsizetype end; (end = pending_.indexOf('\n')) >= 0; pending_.remove(0, end + 1)) {
        const auto event = QJsonDocument::fromJson(pending_.left(end)).object();
        const auto state = event.value("state").toString();
        if (state == QLatin1String("slide"))
            emit slideChanged(event.value("slide").toInt(), event.value("notes").toString());
        else if (state == QLatin1String("connecting"))
            emit statusChanged(tr("Connecting to LibreOffice…"));
        else if (state == QLatin1String("waiting"))
            emit statusChanged(tr("Connected to Impress. Start the slideshow; Kalipso follows its slides."));
        else if (state == QLatin1String("ended"))
            emit statusChanged(tr("The slideshow ended. Start it again to keep following."));
        else if (state == QLatin1String("closed"))
            emit statusChanged(tr("The presentation was closed. Stopped following Impress."));
        else if (state == QLatin1String("error"))
            emit statusChanged(tr("Cannot follow Impress: %1").arg(event.value("message").toString()));
    }
}
