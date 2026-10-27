// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <QObject>
#include <QProcess>
#include <QJsonObject>
#include <QStringList>

class SystemSpeechClient : public QObject {
    Q_OBJECT
public:
    explicit SystemSpeechClient(QObject *parent=nullptr, QString executable={});
    ~SystemSpeechClient() override;
    static bool platformSupported();
    void check(const QString &language);
    void install(const QString &language);
    void start(const QString &language, const QStringList &phrases);
    void updateScript(const QStringList &phrases);
    void stop();
    bool busy() const { return process_ != nullptr; }
    static QString helperPath();
signals:
    void capabilities(const QJsonObject &value);
    void transcript(const QString &text, bool final);
    void ready();
    void message(const QString &text);
    void failed(const QString &text);
    void completed();
private:
    void launch(const QString &operation, const QString &language);
    void readOutput();
    void fail(const QString &message);
    QString executable_, operation_;
    QStringList phrases_;
    QProcess *process_=nullptr;
    QByteArray output_;
    int revision_=0;
    bool receivedResult_=false;
};
