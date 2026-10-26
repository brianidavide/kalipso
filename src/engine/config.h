// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QLibrary>
#include <QSettings>
#include <QStandardPaths>

namespace SpeechConfig {
inline bool supported(const QString &backend){return backend=="moonshine"||backend=="vosk";}
inline QString selected(){
    QSettings c;
    // Preserve existing installations; new installations prefer Moonshine.
    return c.value("speech/backend",c.contains("speech/model")?"vosk":"moonshine").toString();
}
inline QVariant value(const QString &backend,const QString &key,const QVariant &fallback={}){
    QSettings c;
    const QString path="profiles/"+backend+"/"+key;
    if(c.contains(path))return c.value(path);
    if(backend=="vosk"&&c.contains("speech/"+key))return c.value("speech/"+key);
    return fallback;
}
inline QString library(const QString &backend){return value(backend,"library",backend=="moonshine"?"libmoonshine.so":"libvosk.so").toString();}
inline QJsonObject dependencies(const QString &backend){
    QSettings c;const auto model=value(backend,"model").toString();const auto lib=library(backend);
    QJsonArray missing;
    if(!supported(backend))missing.append("Unsupported speech engine. Choose Moonshine or Vosk.");
    if(QStandardPaths::findExecutable("pw-record").isEmpty())missing.append("PipeWire capture is missing: install pipewire-audio on Arch or pipewire-bin on Debian.");
    const QStringList symbols=backend=="moonshine"?QStringList{"moonshine_load_transcriber_from_files","moonshine_free_transcriber","moonshine_create_stream","moonshine_start_stream","moonshine_stop_stream","moonshine_free_stream","moonshine_transcribe_add_audio_to_stream","moonshine_transcribe_stream"}:QStringList{"vosk_model_new","vosk_model_free","vosk_recognizer_new","vosk_recognizer_free","vosk_recognizer_accept_waveform","vosk_recognizer_result","vosk_recognizer_partial_result"};
    QLibrary runtime(lib);bool api=supported(backend)&&runtime.load();
    for(const auto &symbol:symbols)api=api&&runtime.resolve(symbol.toLatin1().constData());
    if(!api)missing.append(backend=="moonshine"?"Moonshine library or its ONNX Runtime dependency is missing or incompatible. Install/configure the Moonshine runtime.":"Vosk library is missing or incompatible. Install/configure the Vosk runtime.");
    runtime.unload();
    const QStringList files=backend=="moonshine"?QStringList{"frontend.ort","encoder.ort","adapter.ort","cross_kv.ort","decoder_kv.ort","streaming_config.json","tokenizer.bin"}:QStringList{"am/final.mdl","conf/mfcc.conf"};
    bool modelFiles=!model.isEmpty();
    for(const auto &file:files){
        // Current releases name the frontend graph frontend.model.ort.
        if(backend=="moonshine"&&file=="frontend.ort")modelFiles=modelFiles&&(QFileInfo(model+"/frontend.ort").isFile()||QFileInfo(model+"/frontend.model.ort").isFile());
        else modelFiles=modelFiles&&QFileInfo(model+"/"+file).isFile();
    }
    if(!modelFiles)missing.append(backend=="moonshine"?"Moonshine streaming model is missing. Install a language model and configure its extracted folder.":"Extracted Vosk model is missing. Install a language model and configure its extracted folder.");
    return {{"ready",missing.isEmpty()},{"backend",backend},{"missing",missing},{"library",lib},{"model",model},
        {"language",value(backend,"language").toString()},{"architecture",value(backend,"architecture",2).toInt()},
        {"configFile",c.fileName()},{"modelLoadTested",false},
        {"setup","https://github.com/brianidavide/kalipso/blob/main/docs/local-engine.md"}};
}
inline QJsonArray backends(){
    QJsonArray result;
    for(const auto &id:{QString("moonshine"),QString("vosk")})result.append(QJsonObject{
        {"id",id},{"label",id=="moonshine"?"Moonshine":"Vosk"},{"preferred",id=="moonshine"},
        {"model",value(id,"model").toString()},{"language",value(id,"language").toString()}});
    return result;
}
}
