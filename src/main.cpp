// SPDX-License-Identifier: AGPL-3.0-or-later
#include "settings_migration.h"
#include "window.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QTimer>
#include <QTranslator>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("kalipso");
    app.setOrganizationName("brianidavide");
    app.setApplicationVersion(KALIPSO_VERSION);
    app.setDesktopFileName("io.github.brianidavide.kalipso");
    importLegacySettings("brianidavide", "kde-teleprompter");

    // Interface language: the saved choice, otherwise the system language.
    const auto language = QSettings().value("appearance/language").toString();
    const QLocale locale = language.isEmpty() ? QLocale::system() : QLocale(language);
    QTranslator qtTranslator, translator;
    if (qtTranslator.load(locale, "qtbase", "_", QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtTranslator);
    if (translator.load(locale, "kalipso", "_", ":/i18n"))
        app.installTranslator(&translator);

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("main", "Kalipso — local teleprompter"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOptions({
        {"demo", "Show a sample script without changing your saved scripts."},
        {"prompter", "Show the reading window at startup."},
        {"quit-after", "Exit after the given milliseconds (smoke tests).", "milliseconds"},
        {"screenshot", "Save sample screenshots to a directory, then exit.", "directory"},
    });
    parser.process(app);

    MainWindow window;
    if (parser.isSet("demo") || parser.isSet("screenshot"))
        window.setDemoScript();
    window.show();
    if (parser.isSet("prompter"))
        window.showPrompter();
    if (parser.isSet("quit-after")) {
        bool ok = false;
        const int delay = parser.value("quit-after").toInt(&ok);
        if (ok && delay > 0)
            QTimer::singleShot(delay, &app, &QCoreApplication::quit);
    }
    if (parser.isSet("screenshot")) {
        const QString directory = parser.value("screenshot");
        QDir().mkpath(directory);
        QTimer::singleShot(500, &window, [&window, directory, &app] {
            window.grab().save(directory + "/editor.png");
            window.showPrompter();
            QTimer::singleShot(300, &window, [&window, directory, &app] {
                window.prompter()->grab().save(directory + "/prompter.png");
                app.quit();
            });
        });
    }
    return app.exec();
}
