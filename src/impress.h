// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>

// Follows a LibreOffice Impress slideshow, on request only. Starts LibreOffice on
// the chosen presentation with a local named-pipe connection (no network), and
// runs impress_follow.py with LibreOffice's Python to report the current slide
// and its speaker notes. LibreOffice keeps running when following stops.
class ImpressFollower : public QObject {
    Q_OBJECT
public:
    struct Office {
        QString soffice, python; // Empty when not found.
    };
    explicit ImpressFollower(QObject *parent = nullptr);
    ~ImpressFollower() override;
    static Office findOffice();
    bool start(const QString &presentation);
    // Runs a helper that prints the same JSON lines; start() uses LibreOffice's.
    void startHelper(const QString &program, const QStringList &arguments);
    void stop();
    bool active() const { return helper_.state() != QProcess::NotRunning; }

signals:
    void slideChanged(int number, const QString &notes);
    void statusChanged(const QString &text);
    void finished();

private:
    void readLines();
    QProcess helper_;
    QTemporaryDir scripts_;
    QByteArray pending_;
    bool stopping_ = false;
};
