// SPDX-License-Identifier: AGPL-3.0-or-later
// Matched small text panels; a rendering probe, not an application comparison.
#include <QApplication>
#include <QPainter>
#include <QStaticText>
#include <QQuickView>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QTimer>
#include <QWidget>

class Panel : public QWidget {
public:
    int line = 0;
    QStaticText text[3];
    Panel() {
        resize(640,240);
        text[0].setText("Keep your eyes near the camera.");
        text[1].setText("Listen, then choose your words.");
        text[2].setText("Your script stays yours to edit.");
        auto f=font(); f.setPixelSize(26); setFont(f);
        for(auto &item:text) item.prepare(QTransform{},f);
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.fillRect(rect(),QColor("#18282b"));
        p.fillRect(QRect(16,30+line*52,608,44),QColor("#24665e"));
        p.setPen(QColor("#f5f3ed"));
        for(int i=0;i<3;++i) p.drawStaticText(24,36+i*52,text[i]);
    }
};
int main(int argc,char **argv) {
    QApplication app(argc,argv);
    const auto args=app.arguments(); const bool quick=args.contains("quick");
    const bool active=args.contains("active");
    QTimer timer; timer.setInterval(200);
    Panel *panel=nullptr;
    QQuickView *view=nullptr;
    if(quick) {
        view=new QQuickView; view->resize(640,240);
        QQmlComponent component(view->engine());
        component.setData(R"(
import QtQuick
Rectangle {
  width:640; height:240; color:"#18282b"; property int line:0
  Rectangle {x:16; y:30+parent.line*52; width:608; height:44; color:"#24665e"}
  Text {x:24; y:36; text:"Keep your eyes near the camera."; font.pixelSize:26; color:"#f5f3ed"}
  Text {x:24; y:88; text:"Listen, then choose your words."; font.pixelSize:26; color:"#f5f3ed"}
  Text {x:24; y:140; text:"Your script stays yours to edit."; font.pixelSize:26; color:"#f5f3ed"}
})",QUrl{});
        auto *root=component.create();
        if(!root) { qWarning()<<component.errors(); return 1; }
        view->setContent(QUrl{},&component,root); view->show();
        QObject::connect(&timer,&QTimer::timeout,root,[root]{root->setProperty("line",(root->property("line").toInt()+1)%3);});
    } else {
        panel=new Panel; panel->show();
        QObject::connect(&timer,&QTimer::timeout,panel,[panel]{panel->line=(panel->line+1)%3;panel->update();});
    }
    if(active) timer.start();
    QTimer::singleShot(7000,&app,&QCoreApplication::quit);
    const int result=app.exec(); delete view; delete panel; return result;
}
