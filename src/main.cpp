// SPDX-License-Identifier: AGPL-3.0-or-later
#include "window.h"
#include "settings_migration.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>
#include <QDir>

int main(int argc,char **argv) {
    QApplication app(argc,argv);
    app.setApplicationName("kalipso"); app.setOrganizationName("brianidavide");
    importLegacySettings("brianidavide", "kde-teleprompter");
    app.setApplicationVersion("0.1.0-dev");
    app.setDesktopFileName("io.github.brianidavide.kalipso");
    QCommandLineParser parser; parser.setApplicationDescription("Kalipso — local teleprompter for Linux");
    parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({"demo","Load a synthetic demonstration script (no audio or API)."});
    parser.addOption({"prompter","Show the reading window at startup."});
    parser.addOption({"quit-after","Exit after milliseconds (smoke tests).","milliseconds"});
    parser.addOption({"screenshot","Save synthetic demo screenshots to a directory, then exit.","directory"});
    parser.process(app);
    MainWindow window;
    if(parser.isSet("demo") || parser.isSet("screenshot")) window.setDemoScript();
    window.show();
    if(parser.isSet("prompter")) window.showPrompter();
    if(parser.isSet("quit-after")) {
        bool ok=false; int delay=parser.value("quit-after").toInt(&ok);
        if(ok && delay>0) QTimer::singleShot(delay,&app,&QCoreApplication::quit);
    }
    if(parser.isSet("screenshot")) {
        const QString directory=parser.value("screenshot"); QDir{}.mkpath(directory);
        QTimer::singleShot(500,&window,[&window,directory,&app] {
            window.grab().save(directory+"/editor.png"); window.showPrompter();
            QTimer::singleShot(300,&window,[&window,directory,&app] { window.prompter()->grab().save(directory+"/prompter.png"); app.quit(); });
        });
    }
    return app.exec();
}
