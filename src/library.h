// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <QDateTime>
#include <QString>
#include <QVector>

// Scripts saved automatically as UTF-8 files in the application data folder,
// with each script's reading position. Nothing leaves the device.
class ScriptLibrary {
public:
    struct Entry {
        QString id;
        QString title;
        QDateTime modified;
        int position = -1; // Character offset of the current word; -1 before reading.
        int speed = 0;     // Steady speed in words per minute; 0 uses the general setting.
    };

    explicit ScriptLibrary(QString directory = {});
    QString directory() const { return directory_; }

    QVector<Entry> entries() const; // Most recently modified first.
    QString create(const QString &text = {});
    QString load(const QString &id) const;
    bool save(const QString &id, const QString &text);
    bool remove(const QString &id);
    bool contains(const QString &id) const;
    int position(const QString &id) const;
    // Kept in memory until flush(), so frequent reading updates do not write to disk.
    void setPosition(const QString &id, int character);
    int speed(const QString &id) const;
    void setSpeed(const QString &id, int wordsPerMinute);
    bool flush();

    static QString titleFor(const QString &text);

private:
    QString path(const QString &id) const;
    void readIndex();
    bool writeIndex();
    QString directory_;
    QVector<Entry> entries_;
    bool dirty_ = false;
};
