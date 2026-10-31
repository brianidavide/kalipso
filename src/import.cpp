// SPDX-License-Identifier: AGPL-3.0-or-later
#include "import.h"
#include "zip.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QStringDecoder>
#include <QXmlStreamReader>
#include <algorithm>

namespace {
constexpr qint64 kMaxFile = 200 * 1024 * 1024;
constexpr qint64 kMaxEntry = 64 * 1024 * 1024;
constexpr qint64 kMaxInflated = 256 * 1024 * 1024; // Per archive, against decompression bombs.
constexpr qsizetype kMaxText = 2 * 1024 * 1024;    // Same limit as text files.

QString attribute(const QXmlStreamReader &xml, QStringView name, bool namespaced = false) {
    for (const auto &item : xml.attributes())
        if (item.name() == name && item.namespaceUri().isEmpty() != namespaced)
            return item.value().toString();
    return {};
}

// Resolves "../notesSlides/notesSlide1.xml" relative to "ppt/slides/slide1.xml".
// Targets starting with "/" are relative to the package root. Returns an empty
// path for targets that climb above the root.
QString resolve(const QString &from, const QString &target) {
    if (target.startsWith(u'/'))
        return target.mid(1);
    auto parts = from.split(u'/');
    parts.removeLast();
    for (const auto &part : target.split(u'/')) {
        if (part == QLatin1String("..")) {
            if (parts.isEmpty())
                return {};
            parts.removeLast();
        } else if (part != QLatin1String(".")) {
            parts.append(part);
        }
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
                text += QString(std::clamp(attribute(xml, u"c", true).toInt(), 1, 100), u' ');
            else if (xml.name() == QLatin1String("tab"))
                text += u'\t';
            else if (xml.name() == QLatin1String("line-break"))
                text += u'\n';
        } else if (xml.isEndElement()) {
            --depth;
        } else if (xml.isCharacters()) {
            text += xml.text();
        }
        if (text.size() > kMaxText)
            break;
    }
    return text;
}
// A document paragraph built from runs of text. Bold and italic become Markdown
// emphasis (bold wins when both apply: "***" is ambiguous in the highlighter).
class Paragraph {
public:
    void add(const QString &text, bool bold, bool italic) {
        if (text.isEmpty())
            return;
        if (!runs_.isEmpty() && runs_.last().bold == bold && runs_.last().italic == italic)
            runs_.last().text += text;
        else
            runs_.append({text, bold, italic});
        size_ += text.size();
    }
    qsizetype size() const { return size_; }
    QString plain() const {
        QString text;
        for (const auto &run : runs_)
            text += run.text;
        return text.trimmed();
    }
    QString markdown() const {
        QString text;
        for (const auto &run : runs_) {
            const QString marker = run.bold ? QStringLiteral("**") : run.italic ? QStringLiteral("*") : QString();
            const auto core = run.text.trimmed();
            if (marker.isEmpty() || core.isEmpty()) {
                text += run.text;
                continue;
            }
            // Markers must touch the words, so surrounding spaces stay outside them.
            const auto lead = run.text.indexOf(core);
            text += run.text.left(lead) + marker + core + marker + run.text.mid(lead + core.size());
        }
        return text.trimmed();
    }

private:
    struct Run {
        QString text;
        bool bold, italic;
    };
    QList<Run> runs_;
    qsizetype size_ = 0;
};

// Collects document paragraphs as Markdown: "#" headings, "-" list items, blank lines between blocks.
class Blocks {
public:
    void add(const Paragraph &paragraph, int heading, bool list) {
        collected_ += paragraph.size();
        auto text = heading ? paragraph.plain() : paragraph.markdown();
        if (text.isEmpty())
            return;
        if (heading)
            text = QString(std::clamp(heading, 1, 6), u'#') + u' ' + text.simplified();
        else if (list)
            text = QStringLiteral("- ") + text;
        list = list && !heading;
        if (!text_.isEmpty())
            text_ += list && previousList_ ? QStringLiteral("\n") : QStringLiteral("\n\n");
        text_ += text;
        previousList_ = list;
    }
    bool full() const { return collected_ > kMaxText || text_.size() > kMaxText; }
    QString text() const { return text_; }

private:
    QString text_;
    qsizetype collected_ = 0;
    bool previousList_ = false;
};

bool switchedOn(const QXmlStreamReader &xml) {
    const auto value = attribute(xml, u"val", true);
    return value.isEmpty() || (value != QLatin1String("0") && value != QLatin1String("false") &&
                               value != QLatin1String("off") && value != QLatin1String("none"));
}

struct Emphasis {
    bool bold = false, italic = false;
};
} // namespace

namespace ScriptImport {
QString dialogFilter() {
    return QCoreApplication::translate(
        "ScriptImport", "Scripts, documents and presentations (*.txt *.md *.markdown *.docx *.odt *.pptx *.odp);;"
                        "Text (*.txt *.md *.markdown);;Word (*.docx);;OpenDocument text (*.odt);;"
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
    if (suffix == QLatin1String("docx"))
        return fromDocx(data);
    if (suffix == QLatin1String("odt"))
        return fromOdt(data);
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
    const ZipArchive zip(archive, {kMaxEntry, kMaxInflated});
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
    QSet<QString> seen;
    for (int i = 0; i < order.size() && result.text.size() <= kMaxText; ++i) {
        if (order[i].isEmpty() || seen.contains(order[i]))
            continue;
        seen.insert(order[i]);
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
    if (result.text.size() > kMaxText)
        return {{}, QCoreApplication::translate("ScriptImport", "These speaker notes are too long to import."), 0};
    if (!result.sections)
        result.error = QCoreApplication::translate("ScriptImport", "This presentation has no speaker notes.");
    return result;
}

Result fromOdp(const QByteArray &archive) {
    const ZipArchive zip(archive, {kMaxEntry, kMaxInflated});
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
    qsizetype collected = 0;
    while (!xml.atEnd() && collected <= kMaxText) {
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
                collected += paragraph.size();
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
    if (collected > kMaxText)
        return {{}, QCoreApplication::translate("ScriptImport", "These speaker notes are too long to import."), 0};
    if (xml.hasError())
        return {{}, QCoreApplication::translate("ScriptImport", "This presentation could not be read."), 0};
    if (result.text.size() > kMaxText)
        return {{}, QCoreApplication::translate("ScriptImport", "These speaker notes are too long to import."), 0};
    if (!result.sections)
        result.error = QCoreApplication::translate("ScriptImport", "This presentation has no speaker notes.");
    return result;
}

Result fromDocx(const QByteArray &archive) {
    const ZipArchive zip(archive, {kMaxEntry, kMaxInflated});
    const QString main = "word/document.xml";
    if (!zip.valid() || !zip.contains(main))
        return {{}, QCoreApplication::translate("ScriptImport", "This is not a readable Word (.docx) document."), 0};
    // Heading levels by style id. Word writes style names in English ("heading 1",
    // "Title") whatever the interface language; the ids may be translated.
    QHash<QString, int> headingStyles;
    QXmlStreamReader styles(zip.file("word/styles.xml"));
    QString style;
    while (!styles.atEnd()) {
        styles.readNext();
        if (styles.isStartElement()) {
            if (styles.name() == QLatin1String("style")) {
                style = attribute(styles, u"styleId", true);
            } else if (styles.name() == QLatin1String("name") && !style.isEmpty()) {
                const auto name = attribute(styles, u"val", true).toLower();
                if (name == QLatin1String("title"))
                    headingStyles.insert(style, 1);
                else if (name.startsWith(QLatin1String("heading ")))
                    headingStyles.insert(style, std::clamp(name.mid(8).toInt(), 1, 6));
            } else if (styles.name() == QLatin1String("outlineLvl") && !style.isEmpty()) {
                const int level = attribute(styles, u"val", true).toInt();
                if (level >= 0 && level < 6 && !headingStyles.contains(style))
                    headingStyles.insert(style, level + 1);
            }
        } else if (styles.isEndElement() && styles.name() == QLatin1String("style")) {
            style.clear();
        }
    }

    QXmlStreamReader xml(zip.file(main));
    Blocks blocks;
    while (!xml.atEnd() && !blocks.full()) {
        xml.readNext();
        if (!xml.isStartElement() || xml.name() != QLatin1String("p"))
            continue;
        Paragraph paragraph;
        int heading = 0;
        bool list = false;
        Emphasis run;
        for (int depth = 1; depth > 0 && !xml.atEnd();) {
            xml.readNext();
            if (xml.isEndElement()) {
                --depth;
                continue;
            }
            if (!xml.isStartElement())
                continue;
            ++depth;
            const auto name = xml.name();
            if (name == QLatin1String("pStyle")) {
                heading = headingStyles.value(attribute(xml, u"val", true));
            } else if (name == QLatin1String("outlineLvl")) {
                const int level = attribute(xml, u"val", true).toInt();
                heading = level >= 0 && level < 6 ? level + 1 : 0;
            } else if (name == QLatin1String("numId")) {
                list = attribute(xml, u"val", true).toInt() != 0; // Numbering 0 means "not a list item".
            } else if (name == QLatin1String("r")) {
                run = {};
            } else if (name == QLatin1String("b")) {
                run.bold = switchedOn(xml);
            } else if (name == QLatin1String("i")) {
                run.italic = switchedOn(xml);
            } else if (name == QLatin1String("t")) {
                paragraph.add(xml.readElementText(), run.bold, run.italic);
                --depth; // readElementText() consumed the end element.
            } else if (name == QLatin1String("tab")) {
                paragraph.add(QStringLiteral("\t"), run.bold, run.italic);
            } else if (name == QLatin1String("br") || name == QLatin1String("cr")) {
                paragraph.add(QStringLiteral("\n"), false, false);
            }
            // Deleted text (w:delText) and field codes (w:instrText) are not "t", so they are skipped.
        }
        blocks.add(paragraph, heading, list);
    }
    if (blocks.full())
        return {{}, QCoreApplication::translate("ScriptImport", "This document is too long to import."), 0};
    if (xml.hasError())
        return {{}, QCoreApplication::translate("ScriptImport", "This document could not be read."), 0};
    if (blocks.text().isEmpty())
        return {{}, QCoreApplication::translate("ScriptImport", "This document has no text."), 0};
    return {blocks.text(), {}, 0};
}

Result fromOdt(const QByteArray &archive) {
    const ZipArchive zip(archive, {kMaxEntry, kMaxInflated});
    if (!zip.valid() || !zip.contains("content.xml"))
        return {{},
                QCoreApplication::translate("ScriptImport", "This is not a readable OpenDocument text (.odt) file."),
                0};
    QXmlStreamReader xml(zip.file("content.xml"));
    QHash<QString, Emphasis> textStyles; // Automatic styles, where direct bold and italic live.
    QString style;
    int lists = 0;
    Blocks blocks;
    while (!xml.atEnd() && !blocks.full()) {
        xml.readNext();
        const auto name = xml.name();
        if (xml.isEndElement()) {
            if (name == QLatin1String("style"))
                style.clear();
            else if (name == QLatin1String("list"))
                lists = std::max(0, lists - 1);
            continue;
        }
        if (!xml.isStartElement())
            continue;
        if (name == QLatin1String("tracked-changes") || name == QLatin1String("table-of-content") ||
            name == QLatin1String("annotation") || name == QLatin1String("note")) {
            xml.skipCurrentElement(); // Deleted text, generated indexes, comments and footnotes.
        } else if (name == QLatin1String("style")) {
            style = attribute(xml, u"name", true);
        } else if (name == QLatin1String("text-properties") && !style.isEmpty()) {
            const auto weight = attribute(xml, u"font-weight", true);
            const auto slant = attribute(xml, u"font-style", true);
            textStyles.insert(style, {weight == QLatin1String("bold") || weight.toInt() >= 600,
                                      slant == QLatin1String("italic") || slant == QLatin1String("oblique")});
        } else if (name == QLatin1String("list")) {
            ++lists;
        } else if (name == QLatin1String("p") || name == QLatin1String("h")) {
            const int heading =
                name == QLatin1String("h") ? std::max(1, attribute(xml, u"outline-level", true).toInt()) : 0;
            Paragraph paragraph;
            QList<Emphasis> spans{{}};
            for (int depth = 1; depth > 0 && !xml.atEnd();) {
                xml.readNext();
                if (xml.isStartElement()) {
                    const auto inner = xml.name();
                    if (inner == QLatin1String("annotation") || inner == QLatin1String("note")) {
                        xml.skipCurrentElement();
                        continue;
                    }
                    ++depth;
                    auto current = spans.last();
                    if (inner == QLatin1String("span")) {
                        const auto found = textStyles.value(attribute(xml, u"style-name", true));
                        current = {current.bold || found.bold, current.italic || found.italic};
                    }
                    spans.append(current);
                    if (inner == QLatin1String("s"))
                        paragraph.add(QString(std::clamp(attribute(xml, u"c", true).toInt(), 1, 100), u' '),
                                      current.bold, current.italic);
                    else if (inner == QLatin1String("tab"))
                        paragraph.add(QStringLiteral("\t"), current.bold, current.italic);
                    else if (inner == QLatin1String("line-break"))
                        paragraph.add(QStringLiteral("\n"), false, false);
                } else if (xml.isEndElement()) {
                    --depth;
                    spans.removeLast();
                } else if (xml.isCharacters() && !spans.isEmpty()) {
                    paragraph.add(xml.text().toString(), spans.last().bold, spans.last().italic);
                }
                if (paragraph.size() > kMaxText)
                    break;
            }
            blocks.add(paragraph, heading, lists > 0);
        }
    }
    if (blocks.full())
        return {{}, QCoreApplication::translate("ScriptImport", "This document is too long to import."), 0};
    if (xml.hasError())
        return {{}, QCoreApplication::translate("ScriptImport", "This document could not be read."), 0};
    if (blocks.text().isEmpty())
        return {{}, QCoreApplication::translate("ScriptImport", "This document has no text."), 0};
    return {blocks.text(), {}, 0};
}
} // namespace ScriptImport
