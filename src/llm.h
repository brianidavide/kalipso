// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <QNetworkAccessManager>
#include <QPointer>
#include <QNetworkReply>

class LlmClient : public QObject {
    Q_OBJECT
public:
    explicit LlmClient(QObject *parent=nullptr);
    static QString validateEndpoint(const QUrl &url);
    void generate(quint64 id, const QUrl &url, const QString &model, const QString &key,
                  const QString &question, const QString &context);
    void cancel();
signals:
    void completed(quint64 id, const QString &answer);
    void failed(quint64 id, const QString &message);
private:
    QNetworkAccessManager manager_;
    QPointer<QNetworkReply> reply_;
};
