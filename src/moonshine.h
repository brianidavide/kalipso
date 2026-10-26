// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <QString>
#include <QByteArray>
#include <QList>
#include <memory>

// Native adapter: no Python, network requests or model downloads at runtime.
class MoonshineRecognizer {
public:
    struct Event {QString text; bool final;};
    MoonshineRecognizer();
    ~MoonshineRecognizer();
    bool load(const QString &library, const QString &model, const QString &language,
              int architecture, bool meeting, QString &error);
    bool consume(int channel, const QByteArray &pcm, QList<Event> &events, QString &error);
    void unload();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
