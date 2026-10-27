// SPDX-License-Identifier: AGPL-3.0-or-later
#include "system_speech/client.h"
#include "system_speech/grammar.h"
#include "pcm_converter.h"
#include <QSignalSpy>
#include <QtTest>
class SystemSpeechTests:public QObject {
    Q_OBJECT
private slots:
    void probeDoesNotStartRecognition(){
        SystemSpeechClient client(nullptr,FAKE_SYSTEM_SPEECH);QSignalSpy caps(&client,&SystemSpeechClient::capabilities),ready(&client,&SystemSpeechClient::ready),done(&client,&SystemSpeechClient::completed);
        client.check("en-US");QTRY_COMPARE(done.size(),1);QCOMPARE(caps.size(),1);QCOMPARE(ready.size(),0);QVERIFY(!client.busy());
    }
    void explicitInstallCompletesWithoutListening(){
        SystemSpeechClient client(nullptr,FAKE_SYSTEM_SPEECH);QSignalSpy done(&client,&SystemSpeechClient::completed),ready(&client,&SystemSpeechClient::ready);
        client.install("en-US");QTRY_COMPARE(done.size(),1);QCOMPARE(ready.size(),0);QVERIFY(!client.busy());
    }
    void malformedOutputAndPrematureExitFail(){
        for(const auto &language:{"bad-json","too-large","no-result"}){
            SystemSpeechClient client(nullptr,FAKE_SYSTEM_SPEECH);QSignalSpy failure(&client,&SystemSpeechClient::failed);
            client.check(language);QTRY_COMPARE(failure.size(),1);QVERIFY(!client.busy());
        }
    }
    void cancelledStartCannotAdvance(){
        SystemSpeechClient client(nullptr,FAKE_SYSTEM_SPEECH);QSignalSpy words(&client,&SystemSpeechClient::transcript);
        client.start("en-US",{"hello"});client.stop();QTest::qWait(200);QCOMPARE(words.size(),0);QVERIFY(!client.busy());
    }
    void obsoleteGrammarResultsAreDiscarded(){
        SystemSpeechClient client(nullptr,FAKE_SYSTEM_SPEECH);QSignalSpy words(&client,&SystemSpeechClient::transcript);
        client.start("en-US",{"current"});QTRY_VERIFY(!words.isEmpty());
#ifndef Q_OS_MACOS
        QCOMPARE(words.size(),1);
#endif
        QCOMPARE(words.last().at(0).toString(),QString("current"));client.stop();
    }
    void grammarTracksEditedReadingWindow(){
        QString script;for(int i=0;i<300;++i)script+="word"+QString::number(i)+' ';
        const auto phrases=scriptPhrases(script,150);QVERIFY(phrases.size()<=256);QVERIFY(phrases.contains("word151"));QVERIFY(!phrases.contains("word0"));
        QVERIFY(scriptPhrases("",-1).isEmpty());QVERIFY(scriptPhrases("hello <tag> world",-1).contains("hello tag world"));
        const auto changed=scriptPhrases("prefix "+script,151);QCOMPARE(changed,phrases);
    }
    void pcmConversionPreservesChunkBoundaries(){
        QByteArray source;
        for(int i=0;i<480;++i){char frame[4];qToLittleEndian(qint16(i*10),frame);qToLittleEndian(qint16(i*10),frame+2);source.append(frame,4);}
        PcmConverter a,b;a.reset(48000,2,PcmConverter::Signed16);b.reset(48000,2,PcmConverter::Signed16);
        const auto expected=a.convert(source);QCOMPARE(expected.size(),320);QByteArray chunked;
        for(int i=0;i<source.size();i+=7)chunked+=b.convert(source.mid(i,7));QCOMPARE(chunked,expected);
        a.reset(16000,1,PcmConverter::Signed16);QCOMPARE(a.convert(QByteArray(320,0)),QByteArray(320,0));
    }
};
QTEST_GUILESS_MAIN(SystemSpeechTests)
#include "system_speech_tests.moc"
