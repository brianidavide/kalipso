// SPDX-License-Identifier: AGPL-3.0-or-later
#include "library.h"
#include "tracker.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>

namespace {
const QString kIndex = QStringLiteral("library.json");
bool validId(const QString &id) {
    static const QRegularExpression pattern("^[0-9a-f]{32}$");
    return pattern.match(id).hasMatch();
}
} // namespace

ScriptLibrary::ScriptLibrary(QString directory) : directory_(std::move(directory)) {
    if (directory_.isEmpty())
        directory_ = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/scripts";
    QDir().mkpath(directory_);
    readIndex();
}

QString ScriptLibrary::titleFor(const QString &text) {
    const auto line = ScriptTracker::title(text);
    if (line.isEmpty())
        return QObject::tr("Untitled script");
    return line.size() > 60 ? line.left(59) + QChar(0x2026) : line;
}

QString ScriptLibrary::path(const QString &id) const { return directory_ + "/" + id + ".txt"; }

void ScriptLibrary::readIndex() {
    entries_.clear();
    QFile file(directory_ + "/" + kIndex);
    if (file.open(QIODevice::ReadOnly)) {
        for (const auto &value : QJsonDocument::fromJson(file.readAll()).array()) {
            const auto object = value.toObject();
            Entry entry{object.value("id").toString(), object.value("title").toString(),
                        QDateTime::fromString(object.value("modified").toString(), Qt::ISODate),
                        object.value("position").toInt(-1), object.value("speed").toInt(0)};
            if (validId(entry.id) && QFile::exists(path(entry.id)))
                entries_.push_back(entry);
        }
    }
    // Recover scripts whose index entry was lost.
    for (const auto &info : QDir(directory_).entryInfoList({"*.txt"}, QDir::Files)) {
        const auto id = info.completeBaseName();
        if (validId(id) && !contains(id))
            entries_.push_back({id, titleFor(load(id)), info.lastModified(), -1, 0});
    }
}

bool ScriptLibrary::writeIndex() {
    QJsonArray array;
    for (const auto &entry : entries_)
        array.append(QJsonObject{{"id", entry.id},
                                 {"title", entry.title},
                                 {"modified", entry.modified.toString(Qt::ISODate)},
                                 {"position", entry.position},
                                 {"speed", entry.speed}});
    QSaveFile file(directory_ + "/" + kIndex);
    const auto data = QJsonDocument(array).toJson();
    dirty_ = false;
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}

QVector<ScriptLibrary::Entry> ScriptLibrary::entries() const {
    auto sorted = entries_;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const auto &a, const auto &b) { return a.modified > b.modified; });
    return sorted;
}

bool ScriptLibrary::contains(const QString &id) const {
    return std::any_of(entries_.begin(), entries_.end(), [&id](const auto &entry) { return entry.id == id; });
}

QString ScriptLibrary::create(const QString &text) {
    const auto id = QUuid::createUuid().toString(QUuid::Id128);
    entries_.push_back({id, titleFor(text), QDateTime::currentDateTimeUtc(), -1, 0});
    if (!save(id, text)) {
        entries_.removeLast();
        return {};
    }
    return id;
}

QString ScriptLibrary::load(const QString &id) const {
    if (!validId(id))
        return {};
    QFile file(path(id));
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

bool ScriptLibrary::save(const QString &id, const QString &text) {
    if (!validId(id))
        return false;
    QSaveFile file(path(id));
    const auto data = text.toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        return false;
    for (auto &entry : entries_)
        if (entry.id == id) {
            entry.title = titleFor(text);
            entry.modified = QDateTime::currentDateTimeUtc();
        }
    return writeIndex();
}

bool ScriptLibrary::remove(const QString &id) {
    if (!validId(id))
        return false;
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(), [&id](const auto &entry) { return entry.id == id; }),
        entries_.end());
    return QFile::remove(path(id)) && writeIndex();
}

int ScriptLibrary::position(const QString &id) const {
    for (const auto &entry : entries_)
        if (entry.id == id)
            return entry.position;
    return -1;
}

void ScriptLibrary::setPosition(const QString &id, int character) {
    for (auto &entry : entries_)
        if (entry.id == id && entry.position != character) {
            entry.position = character;
            dirty_ = true;
        }
}

int ScriptLibrary::speed(const QString &id) const {
    for (const auto &entry : entries_)
        if (entry.id == id)
            return entry.speed;
    return 0;
}

void ScriptLibrary::setSpeed(const QString &id, int wordsPerMinute) {
    for (auto &entry : entries_)
        if (entry.id == id && entry.speed != wordsPerMinute) {
            entry.speed = wordsPerMinute;
            dirty_ = true;
        }
}

bool ScriptLibrary::flush() {
    if (!dirty_)
        return true;
    dirty_ = !writeIndex();
    return !dirty_;
}
