// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QtEndian>

extern "C" {
#include "../third_party/puff/puff.h"
}

// Guards against decompression bombs: one member, and everything read from one archive.
struct ZipLimits {
    qint64 entry = 64 * 1024 * 1024;
    qint64 total = 256 * 1024 * 1024;
};

// Reads stored and deflated members of a ZIP archive held in memory (no ZIP64, no encryption).
class ZipArchive {
public:
    explicit ZipArchive(const QByteArray &data, ZipLimits limits = {}) : data_(data), limits_(limits) { parse(); }
    bool valid() const { return valid_; }
    bool contains(const QString &name) const { return entries_.contains(name); }
    // Member names in archive order.
    QStringList names() const { return order_; }
    // A symbolic link stored by a Unix zip tool: never extracted as a file.
    bool isLink(const QString &name) const {
        const auto found = entries_.constFind(name);
        return found != entries_.cend() && found->madeOnUnix && ((found->attributes >> 16) & 0170000) == 0120000;
    }

    // The member's bytes; on failure (missing, damaged, over the limits) an empty array and *ok false.
    QByteArray file(const QString &name, bool *ok = nullptr) const {
        if (ok)
            *ok = false;
        const auto found = entries_.constFind(name);
        if (found == entries_.cend())
            return {};
        const auto &entry = *found;
        const qint64 header = entry.offset;
        if (header + 30 > data_.size() || u32(header) != 0x04034b50)
            return {};
        const qint64 start = header + 30 + u16(header + 26) + u16(header + 28);
        if (start + entry.compressed > data_.size())
            return {};
        const auto *source = reinterpret_cast<const unsigned char *>(data_.constData() + start);
        if (entry.method == 0) {
            if (entry.compressed != entry.size)
                return {};
            if (ok)
                *ok = true;
            return QByteArray(reinterpret_cast<const char *>(source), entry.size);
        }
        if (entry.method != 8 || entry.size > limits_.entry || inflated_ + entry.size > limits_.total)
            return {};
        inflated_ += entry.size;
        QByteArray output(qsizetype(entry.size), Qt::Uninitialized);
        unsigned long outputSize = entry.size, sourceSize = entry.compressed;
        const int status = puff(reinterpret_cast<unsigned char *>(output.data()), &outputSize, source, &sourceSize);
        if (status != 0 || outputSize != entry.size)
            return {};
        if (ok)
            *ok = true;
        return output;
    }

private:
    struct Entry {
        quint16 method;
        quint32 compressed, size;
        qint64 offset;
        bool madeOnUnix;
        quint32 attributes;
    };
    quint16 u16(qint64 at) const { return qFromLittleEndian<quint16>(data_.constData() + at); }
    quint32 u32(qint64 at) const { return qFromLittleEndian<quint32>(data_.constData() + at); }

    void parse() {
        // The end-of-central-directory record is within the last 64 KiB + 22 bytes.
        qint64 end = -1;
        for (qint64 at = data_.size() - 22; at >= 0 && at >= data_.size() - 65557; --at)
            if (u32(at) == 0x06054b50) {
                end = at;
                break;
            }
        if (end < 0)
            return;
        const int count = u16(end + 10);
        qint64 at = u32(end + 16);
        for (int i = 0; i < count; ++i) {
            if (at + 46 > data_.size() || u32(at) != 0x02014b50)
                return;
            const int nameLength = u16(at + 28);
            if (at + 46 + nameLength > data_.size())
                return;
            const auto name = QString::fromUtf8(data_.constData() + at + 46, nameLength);
            const quint32 compressed = u32(at + 20), size = u32(at + 24), offset = u32(at + 42);
            if (compressed == 0xffffffff || size == 0xffffffff || offset == 0xffffffff)
                return; // ZIP64: not supported.
            if (!entries_.contains(name))
                order_.append(name);
            entries_.insert(name,
                            {u16(at + 10), compressed, size, qint64(offset), (u16(at + 4) >> 8) == 3, u32(at + 38)});
            at += 46 + nameLength + u16(at + 30) + u16(at + 32);
        }
        valid_ = true;
    }

    const QByteArray &data_;
    ZipLimits limits_;
    QHash<QString, Entry> entries_;
    QStringList order_;
    mutable qint64 inflated_ = 0;
    bool valid_ = false;
};
