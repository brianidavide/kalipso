// SPDX-License-Identifier: AGPL-3.0-or-later
#include <QCoreApplication>
#include <QTimer>
#include <iostream>
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);const auto args=app.arguments();if(args.size()!=3)return 2;
    if(args[2]=="bad-json"){std::cout<<"not json\n"<<std::flush;return 0;}
    if(args[2]=="too-large"){std::cout<<std::string(70000,'x')<<std::flush;return 0;}
    if(args[2]=="no-result")return 0;
    if(args[1]=="--check"){std::cout<<"{\"type\":\"capabilities\",\"available\":false,\"installed\":false,\"supported\":true}\n"<<std::flush;return 0;}
    if(args[1]=="--install"){std::cout<<"{\"type\":\"complete\"}\n"<<std::flush;return 0;}
    QTimer::singleShot(100,[]{
        std::cout<<"{\"type\":\"ready\"}\n";
        std::cout<<"{\"type\":\"transcript\",\"revision\":0,\"text\":\"obsolete\",\"final\":true}\n";
#ifdef Q_OS_MACOS
        std::cout<<"{\"type\":\"transcript\",\"text\":\"current\",\"final\":true}\n"<<std::flush;
#else
        std::cout<<"{\"type\":\"transcript\",\"revision\":1,\"text\":\"current\",\"final\":true}\n"<<std::flush;
#endif
    });
    return app.exec();
}
