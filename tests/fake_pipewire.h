// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// A stand-in for PipeWire's command-line tools, first on PATH while it lives. pw-dump prints the
// chosen list of nodes; pw-record writes down its arguments and sends the chosen sound (16 kHz mono
// 16-bit PCM) again and again, or fails with a message.
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTemporaryDir>
#include <QtEndian>
#include <cmath>
#include <numbers>

class FakePipeWire {
public:
    struct Node {
        QString name, description, mediaClass = QStringLiteral("Audio/Source");
    };

    FakePipeWire() : path_(qgetenv("PATH")) {
        script("pw-dump", "#!/bin/sh\nexec cat \"$(dirname \"$0\")/dump.json\"\n");
        script("pw-record",
               "#!/bin/sh\nhere=$(dirname \"$0\")\nprintf '%s\\n' \"$@\" > \"$here/arguments\"\n"
               "if [ -f \"$here/failure\" ]; then cat \"$here/audio.raw\" \"$here/failure\" >&2; exit 1; fi\n"
               "while :; do cat \"$here/audio.raw\"; sleep 0.1; done\n");
        setNodes({}, {});
        setSound(QByteArray());
        qputenv("PATH", dir_.path().toLocal8Bit() + ':' + path_);
    }
    ~FakePipeWire() { qputenv("PATH", path_); }

    void setNodes(const QList<Node> &nodes, const QString &defaultSource) {
        QJsonArray dump;
        int id = 30;
        for (const auto &node : nodes)
            dump.append(QJsonObject{{"id", ++id},
                                    {"type", "PipeWire:Interface:Node"},
                                    {"info", QJsonObject{{"props", QJsonObject{{"node.name", node.name},
                                                                               {"node.description", node.description},
                                                                               {"media.class", node.mediaClass}}}}}});
        QJsonArray metadata;
        if (!defaultSource.isEmpty())
            metadata.append(QJsonObject{{"subject", 0},
                                        {"key", "default.audio.source"},
                                        {"type", "Spa:String:JSON"},
                                        {"value", QJsonObject{{"name", defaultSource}}}});
        dump.append(QJsonObject{{"id", ++id},
                                {"type", "PipeWire:Interface:Metadata"},
                                {"props", QJsonObject{{"metadata.name", "default"}}},
                                {"metadata", metadata}});
        write("dump.json", QJsonDocument(dump).toJson());
    }
    void setSound(const QByteArray &pcm) { write("audio.raw", pcm); }
    void failWith(const QByteArray &message) { write("failure", message); }
    // The arguments of the last pw-record, or none if it did not run.
    QStringList recordArguments() const {
        QFile file(dir_.filePath("arguments"));
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts)
                                              : QStringList();
    }

    // A 440 Hz tone at the given peak (0 to 1 of full scale); 0 is a muted microphone.
    static QByteArray tone(double peak, int milliseconds) {
        QByteArray pcm(16 * milliseconds * 2, Qt::Uninitialized);
        for (int i = 0; i < 16 * milliseconds; ++i)
            qToLittleEndian(qint16(std::lround(peak * 32767 * std::sin(2 * std::numbers::pi * 440 * i / 16000.0))),
                            pcm.data() + i * 2);
        return pcm;
    }

private:
    void write(const QString &name, const QByteArray &content) {
        QFile file(dir_.filePath(name));
        if (file.open(QIODevice::WriteOnly))
            file.write(content);
    }
    void script(const QString &name, const QByteArray &content) {
        write(name, content);
        QFile(dir_.filePath(name)).setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    }

    QTemporaryDir dir_;
    QByteArray path_;
};
