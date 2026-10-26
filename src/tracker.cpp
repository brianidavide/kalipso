// SPDX-License-Identifier: AGPL-3.0-or-later
#include "tracker.h"
#include <algorithm>
#include <limits>

QVector<Word> ScriptTracker::tokenize(const QString &text) {
    QVector<Word> words;
    int start=-1,end=0;bool dense=false;
    auto flush=[&]{if(start>=0){words.push_back({text.mid(start,end-start).normalized(QString::NormalizationForm_KC).toLower(),start,end-start});start=-1;}};
    for(int i=0;i<text.size();) {
        char32_t cp=text[i].unicode();int size=1;
        if(text[i].isHighSurrogate() && i+1<text.size() && text[i+1].isLowSurrogate()) {cp=QChar::surrogateToUcs4(text[i],text[i+1]);size=2;}
        const auto category=QChar::category(cp);
        const bool mark=category==QChar::Mark_NonSpacing||category==QChar::Mark_SpacingCombining||category==QChar::Mark_Enclosing;
        if(mark||cp==0x200c||cp==0x200d){if(start>=0)end=i+size;}
        else if(QChar::isLetterOrNumber(cp)) {
            // Same base-character fallback as the store frontends for scripts
            // commonly written without spaces. Units are not linguistic words.
            const bool nextDense=(cp>=0x3400&&cp<=0x9fff)||(cp>=0x20000&&cp<=0x323af)||
                (cp>=0x3040&&cp<=0x30ff)||(cp>=0x31f0&&cp<=0x31ff)||
                (cp>=0xe00&&cp<=0xeff)||(cp>=0x1780&&cp<=0x17ff)||
                (cp>=0x1000&&cp<=0x109f)||(cp>=0xaa60&&cp<=0xaa7f);
            if(start>=0&&(dense||nextDense))flush();
            if(start<0)start=i;
            end=i+size;dense=nextDense;
        } else flush();
        i+=size;
    }
    flush();
    return words;
}
void ScriptTracker::setScript(const QString &text) {
    if (script_ == text) return;
    // Map the reading anchor through a single contiguous editor change. Preserve
    // position when text is inserted before it, including repeated paragraphs.
    int anchor = character(), prefix = 0;
    while (prefix < std::min(script_.size(), text.size()) && script_[prefix] == text[prefix]) ++prefix;
    int suffix = 0;
    while (suffix < script_.size() - prefix && suffix < text.size() - prefix &&
           script_[script_.size()-1-suffix] == text[text.size()-1-suffix]) ++suffix;
    if (anchor >= script_.size()-suffix) anchor += int(text.size()-script_.size());
    else if (anchor >= prefix) anchor = prefix;
    const bool started = position_ >= 0;
    script_ = text; words_ = tokenize(text); lastHypothesis_.clear(); position_ = -1;
    if (started && !words_.empty()) {
        position_ = 0;
        for (int i = 0; i < words_.size() && words_[i].start <= anchor; ++i) position_ = i;
    }
}
void ScriptTracker::seek(int word) {
    position_ = std::clamp(word, -1, std::max(-1, int(words_.size())-1));
    lastHypothesis_.clear();
}
int ScriptTracker::character() const { return position_ < 0 ? 0 : words_[position_].start; }
int ScriptTracker::length() const { return position_ < 0 ? 0 : words_[position_].length; }
bool ScriptTracker::follow(const QString &hypothesis, bool final) {
    if (hypothesis.isEmpty()) { if (final) lastHypothesis_.clear(); return false; }
    if (hypothesis == lastHypothesis_) { if (final) lastHypothesis_.clear(); return false; }
    lastHypothesis_ = final ? QString{} : hypothesis;
    auto spoken = tokenize(hypothesis);
    if (spoken.isEmpty() || words_.isEmpty()) return false;
    if (spoken.size() == 1) {
        const int next = position_ + 1;
        if (next < words_.size() && words_[next].normalized == spoken.front().normalized) {
            position_ = next; return true;
        }
        return false;
    }
    // Bounded local sequence matching: no language model and no whole-document
    // scan for each partial transcript. Require >=2 exact words and <=1/3 edits.
    if (spoken.size() > 8) spoken = spoken.sliced(spoken.size()-8);
    const int m = int(spoken.size());
    double best = std::numeric_limits<double>::max();
    int bestEnd = position_;
    const int first = std::max(0, position_-24), last = std::min(int(words_.size()), position_+81);
    for (int start=first; start<last; ++start) {
        QVector<int> previous(m+1), current(m+1);
        for (int i=0; i<=m; ++i) previous[i]=i;
        for (int n=1; n<=m+2 && start+n<=last; ++n) {
            current[0]=n;
            for (int i=1; i<=m; ++i)
                current[i]=std::min({previous[i]+1, current[i-1]+1,
                    previous[i-1]+(spoken[i-1].normalized != words_[start+n-1].normalized)});
            if (n >= std::max(2,m-2) && current[m] <= m/3) {
                int exact = 0;
                for (const auto &word : spoken)
                    for (int j=start; j<start+n; ++j)
                        if (word.normalized == words_[j].normalized) { ++exact; break; }
                if (exact >= 2) {
                    const int end = start+n-1;
                    const double score = current[m] * 100.0 + std::abs(end-position_) * 0.1;
                    if (score < best) { best=score; bestEnd=end; }
                }
            }
            previous.swap(current);
        }
    }
    if (best == std::numeric_limits<double>::max() || bestEnd == position_) return false;
    position_ = bestEnd; return true;
}
