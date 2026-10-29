// SPDX-License-Identifier: AGPL-3.0-or-later
// Time from a recognizer hypothesis to an updated reading position, excluding
// speech recognition itself. Prints JSON: median and p95 per script size.
#include "tracker.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QStringList>
#include <algorithm>
#include <cstdio>

namespace {
QStringList vocabulary() {
    QStringList words;
    for (int i = 0; i < 2000; ++i)
        words << QStringLiteral("w%1").arg(i);
    return words;
}

double percentile(QVector<double> values, double fraction) {
    std::sort(values.begin(), values.end());
    return values[std::min<qsizetype>(values.size() - 1, qsizetype(fraction * values.size()))];
}

// Microseconds per follow() call: in-script speech (local match), off-script
// words absent from the script, and script words in random order (the
// whole-script search runs and fails: the worst case).
QJsonObject measure(int size) {
    const auto words = vocabulary();
    QRandomGenerator random(42);
    QStringList script;
    for (int i = 0; i < size; ++i)
        script << words[random.bounded(words.size())];
    ScriptTracker tracker;
    tracker.setScript(script.join(' '));
    QVector<double> inScript, offScript, scrambled;
    QElapsedTimer timer;
    for (int i = 0; i + 8 < size && inScript.size() < 300; i += 3) {
        const auto hypothesis = script.mid(i, 6).join(' ');
        timer.start();
        tracker.follow(hypothesis, true);
        inScript << timer.nsecsElapsed() / 1000.0;
    }
    for (int i = 0; i < 100; ++i) {
        const auto hypothesis = QStringLiteral("unrelated speech that never appears here %1").arg(i);
        timer.start();
        tracker.follow(hypothesis, true);
        offScript << timer.nsecsElapsed() / 1000.0;
    }
    // Script words in random order: the whole-script search runs and finds nothing.
    for (int i = 0; i < 100; ++i) {
        QStringList hypothesis;
        for (int j = 0; j < 8; ++j)
            hypothesis << script[random.bounded(size)];
        timer.start();
        tracker.follow(hypothesis.join(' '), true);
        scrambled << timer.nsecsElapsed() / 1000.0;
    }
    return {{"words", size},
            {"scrambledMedianUs", percentile(scrambled, 0.5)},
            {"scrambledP95Us", percentile(scrambled, 0.95)},
            {"inScriptMedianUs", percentile(inScript, 0.5)},
            {"inScriptP95Us", percentile(inScript, 0.95)},
            {"offScriptMedianUs", percentile(offScript, 0.5)},
            {"offScriptP95Us", percentile(offScript, 0.95)}};
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QJsonArray results;
    for (const int size : {1000, 10000, 50000})
        results.append(measure(size));
    std::printf("%s\n", QJsonDocument(results).toJson().constData());
    return 0;
}
