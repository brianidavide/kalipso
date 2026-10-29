// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <QByteArray>
#include <QList>
#include <QString>
#include <memory>

// Native adapter for the Moonshine streaming C API. No Python, network
// requests or model downloads at runtime.
class MoonshineRecognizer {
public:
    struct Event {
        QString text;
        bool final;
    };
    MoonshineRecognizer();
    ~MoonshineRecognizer();
    bool load(const QString &library, const QString &model, const QString &language, int architecture,
              QString &error);
    // Accepts 16 kHz signed 16-bit little-endian mono PCM.
    bool consume(const QByteArray &pcm, QList<Event> &events, QString &error);
    void unload();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
