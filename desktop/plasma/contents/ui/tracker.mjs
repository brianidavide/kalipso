// SPDX-License-Identifier: AGPL-3.0-or-later
// Shared source-only tracker for the Plasma widget, the GNOME extension and the
// website. Mirrors src/tracker.cpp; speech comes from the local companion.
import {tokenize as allWords} from './unicode-tokens.mjs';

// [Direction cues] on one line: shown to the reader, never matched to speech.
export function cues(text) {
    const ranges = [];
    let open = -1;
    for (let i = 0; i < text.length; i++) {
        if (text[i] === '[') open = i;
        else if (text[i] === '\n') open = -1;
        else if (text[i] === ']' && open >= 0) {
            ranges.push({start: open, length: i - open + 1});
            open = -1;
        }
    }
    return ranges;
}

// Markdown "# Heading" lines: section titles, never matched to speech.
export function headings(text) {
    const ranges = [];
    let lineStart = 0;
    for (const line of text.split('\n')) {
        if (/^ {0,3}#{1,6}(\s|$)/.test(line)) ranges.push({start: lineStart, length: line.length});
        lineStart += line.length + 1;
    }
    return ranges;
}

// YAML front matter: a first line "---" up to the next "---" or "..." line. Metadata,
// shown dimmed but never read aloud or matched to speech.
export function frontMatter(text) {
    if (!/^---[ \t]*(\n|$)/.test(text)) return null;
    const close = /\n(---|\.\.\.)[ \t]*(\n|$)/g;
    close.lastIndex = 3;
    const match = close.exec(text);
    return match ? {start: 0, length: match.index + 1 + match[1].length} : null;
}

// The script's title: front matter "title:", else its first non-empty line without "#".
export function title(text) {
    const meta = frontMatter(text);
    if (meta) {
        const found = /^title:[ \t]*(.+)$/m.exec(text.slice(0, meta.length));
        if (found) return found[1].trim().replace(/^(["'])(.*)\1$/, '$2');
    }
    return text.slice(meta ? meta.length : 0).split('\n').map(line => line.replace(/^\s*#+\s*/, '').trim()).find(line => line) ?? '';
}

export function sections(text) {
    const meta = frontMatter(text)?.length ?? 0;
    return headings(text).filter(range => range.start >= meta) // A "#" comment in front matter is not a section.
        .map(range => ({title: text.substr(range.start, range.length).trim().replace(/^#+/, '').replace(/#+$/, '').trim(),
            start: range.start}))
        .filter(section => section.title);
}

function silentRanges(text) {
    if (!text.includes('[') && !text.includes('#') && !text.startsWith('---')) return [];
    const meta = frontMatter(text);
    // Cues and headings inside the front matter are part of it.
    const inside = range => meta && range.start < meta.length;
    return [...(meta ? [meta] : []), ...cues(text).filter(r => !inside(r)), ...headings(text).filter(r => !inside(r))]
        .sort((a, b) => a.start - b.start);
}

// Speakable words in order, excluding cues and headings.
export function tokenize(text) {
    const silent = silentRanges(text);
    if (!silent.length) return allWords(text);
    let range = 0;
    return allWords(text).filter(word => {
        while (range < silent.length && silent[range].start + silent[range].length <= word.start) range++;
        return !(range < silent.length && silent[range].start <= word.start);
    });
}

function escape(text) {
    return text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

export class Tracker {
    constructor(text = '') {
        this.text = '';
        this.words = [];
        this.position = -1;
        this.last = '';
        this.misses = 0;
        this.occurrences = new Map();
        this.setText(text);
    }

    tokenize(text) { return tokenize(text); }

    // Several consecutive utterances matched nowhere in the script.
    get lost() { return this.misses >= 2; }

    setText(text) {
        if (text === this.text) return;
        // Map the reading anchor through one contiguous edit.
        let anchor = this.words[this.position]?.start ?? 0, prefix = 0, suffix = 0;
        while (prefix < Math.min(this.text.length, text.length) && this.text[prefix] === text[prefix]) prefix++;
        while (suffix < this.text.length - prefix && suffix < text.length - prefix &&
            this.text[this.text.length - 1 - suffix] === text[text.length - 1 - suffix]) suffix++;
        if (anchor >= this.text.length - suffix) anchor += text.length - this.text.length;
        else if (anchor >= prefix) anchor = prefix;
        const started = this.position >= 0;
        this.text = text;
        this.words = this.tokenize(text);
        this.occurrences = new Map(); // Word -> indexes, for whole-script recovery.
        this.words.forEach((word, index) => {
            if (!this.occurrences.has(word.word)) this.occurrences.set(word.word, []);
            this.occurrences.get(word.word).push(index);
        });
        this.position = -1;
        this.last = '';
        this.misses = 0;
        if (started && this.words.length) {
            this.position = 0;
            this.words.forEach((word, index) => { if (word.start <= anchor) this.position = index; });
        }
    }

    seek(index) {
        this.position = Math.max(-1, Math.min(index, this.words.length - 1));
        this.last = '';
        this.misses = 0;
    }

    // Start of the section to jump to, or -1 without one. Forward: the next heading after
    // the word about to be read. Backward: the section before the current one (at most the
    // first). Measured from the next word because a jump leaves the position just before it.
    sectionTarget(direction) {
        const list = sections(this.text);
        if (!list.length)
            return -1;
        const next = this.position + 1 < this.words.length ? this.words[this.position + 1].start : this.text.length;
        if (direction > 0)
            return list.find(section => section.start > next)?.start ?? -1;
        const before = list.filter(section => section.start < next).length;
        return list[Math.max(0, before - 2)].start;
    }

    // The word at or before a character offset; -1 before the first word.
    seekCharacter(character) {
        let index = -1;
        for (let i = 0; i < this.words.length && this.words[i].start <= character; i++) index = i;
        this.seek(index);
    }

    // Edit distance of `spoken` against script words from `start`, for each length n.
    align(spoken, start, last, accept) {
        const m = spoken.length;
        let previous = Array.from({length: m + 1}, (_, i) => i);
        for (let n = 1; n <= m + 2 && start + n <= last; n++) {
            const current = [n];
            for (let i = 1; i <= m; i++)
                current[i] = Math.min(previous[i] + 1, current[i - 1] + 1,
                    previous[i - 1] + Number(spoken[i - 1].word !== this.words[start + n - 1].word));
            accept(n, current[m]);
            previous = current;
        }
    }

    exactMatches(spoken, start, n) {
        const window = this.words.slice(start, start + n);
        return spoken.filter(word => window.some(candidate => candidate.word === word.word)).length;
    }

    // Bounded search around the reading position: >=2 exact words, <=1 edit per 3.
    localMatch(spoken) {
        const m = spoken.length;
        const first = Math.max(0, this.position - 24), last = Math.min(this.words.length, this.position + 81);
        let best = Infinity, bestEnd = -1;
        for (let start = first; start < last; start++)
            this.align(spoken, start, last, (n, edits) => {
                if (n < Math.max(2, m - 2) || edits > Math.floor(m / 3) || this.exactMatches(spoken, start, n) < 2) return;
                const end = start + n - 1, score = edits * 100 + Math.abs(end - this.position) * 0.1;
                if (score < best) { best = score; bestEnd = end; }
            });
        return bestEnd;
    }

    // Recovers after long skips or backtracking; only an unambiguous strong match.
    // A valid match contains one of the (m - minimumExact + 1) rarest spoken
    // words, so only alignments starting just before their occurrences are checked.
    globalMatch(spoken) {
        const m = spoken.length, minimumExact = Math.max(4, m - Math.floor(m / 4));
        const rare = spoken.map(word => this.occurrences.get(word.word)).filter(Boolean)
            .sort((a, b) => a.length - b.length);
        if (rare.length < minimumExact) return -1;
        const starts = new Set();
        for (const positions of rare.slice(0, m - minimumExact + 1))
            for (const position of positions)
                for (let start = Math.max(0, position - m - 1); start <= position; start++) starts.add(start);
        let bestEdits = Infinity, ends = [];
        for (const start of [...starts].sort((a, b) => a - b))
            this.align(spoken, start, this.words.length, (n, edits) => {
                if (n < m - 1 || edits > Math.floor(m / 4) || edits > bestEdits ||
                    this.exactMatches(spoken, start, n) < minimumExact) return;
                if (edits < bestEdits) { bestEdits = edits; ends = []; }
                ends.push(start + n - 1);
            });
        if (!ends.length || Math.max(...ends) - Math.min(...ends) > 2) return -1;
        return Math.max(...ends);
    }

    follow(text, final = false) {
        if (!text || text === this.last) {
            if (final) this.last = '';
            return false;
        }
        this.last = final ? '' : text;
        let spoken = this.tokenize(text);
        if (!spoken.length || !this.words.length) return false;
        if (spoken.length === 1) {
            if (this.words[this.position + 1]?.word === spoken[0].word) {
                this.position++;
                this.misses = 0;
                return true;
            }
            return false;
        }
        spoken = spoken.slice(-8);
        let end = this.localMatch(spoken);
        if (end < 0 && spoken.length >= 4) end = this.globalMatch(spoken);
        if (end < 0) {
            if (final && spoken.length >= 3) this.misses++;
            return false;
        }
        this.misses = 0;
        if (end === this.position) return false;
        this.position = end;
        return true;
    }

    escaped(text) { return escape(text); }

    // Escaped Pango/Qt markup for text[from, to): cues in italics, headings in bold.
    styled(from, to, collapse = true) {
        let out = '', at = from;
        const plain = value => escape(collapse ? value.replace(/\s+/g, ' ') : value);
        for (const range of silentRanges(this.text)) {
            const start = Math.max(range.start, from), end = Math.min(range.start + range.length, to);
            if (start >= end) continue;
            if (range.start === 0 && frontMatter(this.text)?.length === range.length) {
                at = end; // Metadata is not part of the reading excerpt.
                continue;
            }
            const tag = this.text[range.start] === '[' ? 'i' : 'b';
            out += plain(this.text.slice(at, start)) + `<${tag}>${plain(this.text.slice(start, end))}</${tag}>`;
            at = end;
        }
        return out + plain(this.text.slice(at, to));
    }

    // A bounded excerpt keeps the reading position visible even in long scripts. With `here`
    // ({text, tint, line}) the current word is marked as in the app: text on a tint, underlined in the
    // "you are here" colour. `html` writes Qt rich text (Plasma) instead of Pango markup (GNOME); Qt
    // underlines in the text colour, as it has no separate underline colour.
    readingMarkup(readColor, nextColor, previousWords = 4, here = null, html = false) {
        const first = Math.max(0, this.position - previousWords), last = Math.min(this.words.length, this.position + 13);
        // From the beginning, include a leading heading or cue.
        const start = first === 0 ? 0 : this.words[first]?.start ?? this.text.length;
        const finish = last < this.words.length ? this.words[last].start : this.text.length;
        const word = this.words[this.position];
        const readEnd = word ? word.start + word.length : 0;
        const wordStart = here && word ? Math.max(start, word.start) : readEnd;
        const span = (color, body) => html ? `<font color="${color}">${body}</font>` : `<span foreground="${color}">${body}</span>`;
        const mark = body => html
            ? `<span style="color:${here.text};background-color:${here.tint};text-decoration:underline">${body}</span>`
            : `<span foreground="${here.text}" background="${here.tint}" underline="single" underline_color="${here.line}">${body}</span>`;
        return span(readColor, `${first ? '… ' : ''}${this.styled(start, Math.max(start, wordStart))}`) +
            (wordStart < readEnd ? mark(this.styled(wordStart, readEnd)) : '') +
            span(nextColor, `${this.styled(Math.max(start, readEnd), finish)}${last < this.words.length ? ' …' : ''}`);
    }

    markup(readColor = '#91999f', nextColor = '#f6f8fa') {
        const end = this.position < 0 ? 0 : this.words[this.position].start + this.words[this.position].length;
        return `<span foreground="${readColor}">${this.styled(0, end, false)}</span>` +
            `<span foreground="${nextColor}">${this.styled(end, this.text.length, false)}</span>`;
    }
}
