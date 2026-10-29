// SPDX-License-Identifier: AGPL-3.0-or-later
#include "import.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QStringDecoder>
#include <QXmlStreamReader>
#include <QtEndian>

extern "C" {
#include "../third_party/puff/puff.h"
}

namespace {
constexpr qint64 kMaxFile = 200 * 1024 * 1024;
constexpr quint32 kMaxEntry = 64 * 1024 * 1024;

// Reads stored and deflated members of a ZIP archive (no ZIP64, no encryption).
class ZipArchive {
public:
    explicit ZipArchive(const QByteArray &data) : data_(data) { parse(); }
    bool valid() const { return valid_; }
    bool contains(const QString &name) const { return entries_.contains(name); }

    QByteArray file(const QString &name) const {
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
        if (entry.method == 0)
            return entry.compressed == entry.size ? QByteArray(reinterpret_cast<const char *>(source), entry.size)
                                                  : QByteArray();
        if (entry.method != 8 || entry.size > kMaxEntry)
            return {};
        QByteArray output(qsizetype(entry.size), Qt::Uninitialized);
        unsigned long outputSize = entry.size, sourceSize = entry.compressed;
        const int status = puff(reinterpret_cast<unsigned char *>(output.data()), &outputSize, source, &sourceSize);
        if (status != 0 || outputSize != entry.size)
            return {};
        return output;
    }

private:
    struct Entry {
        quint16 method;
        quint32 compressed, size;
        qint64 offset;
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
            entries_.insert(name, {u16(at + 10), u32(at + 20), u32(at + 24), qint64(u32(at + 42))});
            at += 46 + nameLength + u16(at + 30) + u16(at + 32);
        }
        valid_ = true;
    }

    const QByteArray &data_;
    QHash<QString, Entry> entries_;
    bool valid_ = false;
};

QString attribute(const QXmlStreamReader &xml, QStringView name, bool namespaced = false) {
    for (const auto &item : xml.attributes())
        if (item.name() == name && item.namespaceUri().isEmpty() != namespaced)
            return item.value().toString();
    return {};
}

// Resolves "../notesSlides/notesSlide1.xml" relative to "ppt/slides/slide1.xml".
QString resolve(const QString &from, const QString &target) {
    auto parts = from.split(u'/');
    parts.removeLast();
    for (const auto &part : target.split(u'/')) {
        if (part == QLatin1String(".."))
            parts.removeLast();
        else if (part != QLatin1String("."))
            parts.append(part);
    }
    return parts.join(u'/');
}

QString relsFor(const QString &part) {
    const int slash = int(part.lastIndexOf(u'/'));
    return part.left(slash + 1) + "_rels/" + part.mid(slash + 1) + ".rels";
}

// Relationship id -> resolved part path, optionally filtered by type suffix.
QHash<QString, QString> relationships(const ZipArchive &zip, const QString &part, const QString &typeSuffix = {}) {
    QHash<QString, QString> result;
    QXmlStreamReader xml(zip.file(relsFor(part)));
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == QLatin1String("Relationship") &&
            (typeSuffix.isEmpty() || attribute(xml, u"Type").endsWith(typeSuffix)))
            result.insert(attribute(xml, u"Id"), resolve(part, attribute(xml, u"Target")));
    }
    return result;
}

enum class Shapes { Notes, Title };

// Text of DrawingML shapes. Notes: the body placeholder and any plain text box
// (LibreOffice exports notes that way), never the slide image, number, header,
// footer or date. Title: the title placeholders.
QString shapeText(const QByteArray &part, Shapes kind) {
    static const QStringList notNotes{"sldImg", "sldNum", "hdr", "ftr", "dt"};
    QXmlStreamReader xml(part);
    QStringList paragraphs, shape;
    QString paragraph, type;
    bool inShape = false, placeholder = false, inText = false;
    const auto wanted = [&] {
        if (kind == Shapes::Title)
            return placeholder && (type == QLatin1String("title") || type == QLatin1String("ctrTitle"));
        return !placeholder || !notNotes.contains(type);
    };
    while (!xml.atEnd()) {
        xml.readNext();
        const auto name = xml.name();
        if (xml.isStartElement()) {
            if (name == QLatin1String("sp")) {
                inShape = true;
                placeholder = false;
                type.clear();
                shape.clear();
            } else if (inShape && name == QLatin1String("ph")) {
                placeholder = true;
                type = attribute(xml, u"type");
                if (type.isEmpty())
                    type = QStringLiteral("body");
            } else if (inShape && name == QLatin1String("p")) {
                paragraph.clear();
            } else if (inShape && name == QLatin1String("t")) {
                inText = true;
            } else if (inShape && name == QLatin1String("br")) {
                paragraph += u'\n';
            }
        } else if (xml.isCharacters() && inText) {
            paragraph += xml.text();
        } else if (xml.isEndElement()) {
            if (name == QLatin1String("t")) {
                inText = false;
            } else if (inShape && name == QLatin1String("p")) {
                shape.append(paragraph);
            } else if (name == QLatin1String("sp")) {
                if (wanted())
                    paragraphs += shape;
                inShape = false;
            }
        }
    }
    while (!paragraphs.isEmpty() && paragraphs.last().trimmed().isEmpty())
        paragraphs.removeLast();
    return paragraphs.join(u'\n').trimmed();
}

QString section(int number, const QString &title, const QString &notes) {
    const auto heading =
        title.simplified().isEmpty()
            ? QCoreApplication::translate("ScriptImport", "Slide %1").arg(number)
            : QCoreApplication::translate("ScriptImport", "Slide %1: %2").arg(number).arg(title.simplified());
    return "# " + heading + "\n\n" + notes + "\n";
}

// Text of an ODF paragraph, honouring <text:s text:c="n"/>, tabs and line breaks.
QString odfParagraph(QXmlStreamReader &xml) {
    QString text;
    int depth = 1;
    while (depth > 0 && !xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            ++depth;
            if (xml.name() == QLatin1String("s"))
                text += QString(std::max(1, attribute(xml, u"c", true).toInt()), u' ');
            else if (xml.name() == QLatin1String("tab"))
                text += u'\t';
            else if (xml.name() == QLatin1String("line-break"))
                text += u'\n';
        } else if (xml.isEndElement()) {
            --depth;
        } else if (xml.isCharacters()) {
            text += xml.text();
        }
    }
    return text;
}
} // namespace

namespace ScriptImport {
QString dialogFilter() {
    return QCoreApplication::translate(
        "ScriptImport", "Scripts and presentations (*.txt *.md *.markdown *.pptx *.odp);;Text (*.txt *.md *.markdown);;"
                        "PowerPoint (*.pptx);;LibreOffice Impress (*.odp);;All files (*)");
}

Result read(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {{}, QCoreApplication::translate("ScriptImport", "Cannot open this file."), 0};
    if (file.size() > kMaxFile)
        return {{}, QCoreApplication::translate("ScriptImport", "This file is too large to import."), 0};
    const auto data = file.readAll();
    const auto suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("pptx"))
        return fromPptx(data);
    if (suffix == QLatin1String("odp"))
        return fromOdp(data);
    if (data.size() > 2 * 1024 * 1024)
        return {{}, QCoreApplication::translate("ScriptImport", "Choose a text file smaller than 2 MiB."), 0};
    QStringDecoder utf8(QStringDecoder::Utf8);
    const QString text = utf8.decode(data);
    if (utf8.hasError())
        return {{}, QCoreApplication::translate("ScriptImport", "Choose a UTF-8 text file."), 0};
    QString normalized = text;
    normalized.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    if (normalized.startsWith(QChar(0xfeff)))
        normalized.remove(0, 1);
    return {normalized, {}, 0};
}

Result fromPptx(const QByteArray &archive) {
    const ZipArchive zip(archive);
    const QString presentation = "ppt/presentation.xml";
    if (!zip.valid() || !zip.contains(presentation))
        return {{}, QCoreApplication::translate("ScriptImport", "This is not a readable PowerPoint (.pptx) file."), 0};
    const auto slides = relationships(zip, presentation, "/slide");
    QStringList order;
    QXmlStreamReader xml(zip.file(presentation));
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == QLatin1String("sldId"))
            order.append(slides.value(attribute(xml, u"id", true)));
    }
    Result result;
    for (int i = 0; i < order.size(); ++i) {
        if (order[i].isEmpty())
            continue;
        const auto notesPart = relationships(zip, order[i], "/notesSlide");
        if (notesPart.isEmpty())
            continue;
        const auto notes = shapeText(zip.file(notesPart.cbegin().value()), Shapes::Notes);
        if (notes.isEmpty())
            continue;
        const auto title = shapeText(zip.file(order[i]), Shapes::Title);
        result.text += (result.text.isEmpty() ? QString() : QStringLiteral("\n")) + section(i + 1, title, notes);
        ++result.sections;
    }
    if (!result.sections)
        result.error = QCoreApplication::translate("ScriptImport", "This presentation has no speaker notes.");
    return result;
}

Result fromOdp(const QByteArray &archive) {
    const ZipArchive zip(archive);
    if (!zip.valid() || !zip.contains("content.xml"))
        return {{},
                QCoreApplication::translate("ScriptImport", "This is not a readable LibreOffice Impress (.odp) file."),
                0};
    QXmlStreamReader xml(zip.file("content.xml"));
    Result result;
    int page = 0;
    QString title;
    QStringList notes;
    bool inNotes = false, inTitle = false;
    const auto finishPage = [&] {
        while (!notes.isEmpty() && notes.last().trimmed().isEmpty())
            notes.removeLast();
        const auto text = notes.join(u'\n').trimmed();
        if (!text.isEmpty()) {
            result.text += (result.text.isEmpty() ? QString() : QStringLiteral("\n")) + section(page, title, text);
            ++result.sections;
        }
    };
    while (!xml.atEnd()) {
        xml.readNext();
        const auto name = xml.name();
        if (xml.isStartElement()) {
            if (name == QLatin1String("page") && !inNotes) {
                ++page;
                title.clear();
                notes.clear();
            } else if (name == QLatin1String("notes")) {
                inNotes = true;
            } else if (name == QLatin1String("frame") && !inNotes) {
                inTitle = attribute(xml, u"class", true) == QLatin1String("title");
            } else if (name == QLatin1String("p") || name == QLatin1String("h")) {
                const auto paragraph = odfParagraph(xml);
                if (inNotes)
                    notes.append(paragraph);
                else if (inTitle)
                    title += (title.isEmpty() ? QString() : QStringLiteral(" ")) + paragraph;
            }
        } else if (xml.isEndElement()) {
            if (name == QLatin1String("notes"))
                inNotes = false;
            else if (name == QLatin1String("frame") && !inNotes)
                inTitle = false;
            else if (name == QLatin1String("page") && !inNotes)
                finishPage();
        }
    }
    if (xml.hasError())
        return {{}, QCoreApplication::translate("ScriptImport", "This presentation could not be read."), 0};
    if (!result.sections)
        result.error = QCoreApplication::translate("ScriptImport", "This presentation has no speaker notes.");
    return result;
}
} // namespace ScriptImport
