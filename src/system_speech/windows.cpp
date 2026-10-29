// SPDX-License-Identifier: AGPL-3.0-or-later
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QTimer>
#include <QWinEventNotifier>
#include <functional>
#include <iostream>
#include <sapi.h>
#include <windows.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

static void send(const QJsonObject &event) {
    const auto bytes = QJsonDocument(event).toJson(QJsonDocument::Compact);
    std::cout.write(bytes.constData(), bytes.size());
    std::cout << '\n' << std::flush;
}
static void require(HRESULT code, const char *operation) {
    if (FAILED(code))
        throw QString("%1 failed (0x%2). Check the installed Windows speech language and microphone permissions.")
            .arg(operation)
            .arg(quint32(code), 8, 16, QChar('0'));
}
static QString attribute(ISpObjectToken *token, const wchar_t *name) {
    ComPtr<ISpDataKey> attributes;
    if (FAILED(token->OpenKey(L"Attributes", &attributes)))
        return {};
    wchar_t *text = nullptr;
    if (FAILED(attributes->GetStringValue(name, &text)))
        return {};
    const auto value = QString::fromWCharArray(text);
    CoTaskMemFree(text);
    return value;
}
static ComPtr<ISpObjectTokenCategory> category(const wchar_t *id) {
    ComPtr<ISpObjectTokenCategory> value;
    require(CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&value)),
            "Speech category");
    require(value->SetId(id, FALSE), "Speech category lookup");
    return value;
}
static ComPtr<ISpObjectToken> selectRecognizer(const QString &language, QJsonArray &locales) {
    const auto localeName = language.toStdWString();
    const LCID requested = LocaleNameToLCID(localeName.c_str(), LOCALE_ALLOW_NEUTRAL_NAMES);
    if (!requested)
        throw QString("Choose a valid recognition language, for example en-US or it-IT.");
    ComPtr<IEnumSpObjectTokens> tokens;
    require(category(SPCAT_RECOGNIZERS)->EnumTokens(nullptr, nullptr, &tokens), "Recognizer enumeration");
    ComPtr<ISpObjectToken> chosen;
    while (true) {
        ComPtr<ISpObjectToken> token;
        ULONG fetched = 0;
        if (tokens->Next(1, &token, &fetched) != S_OK || !fetched)
            break;
        // Do not select third-party recognizers whose processing location is unknown.
        if (!attribute(token.Get(), L"Vendor").startsWith("Microsoft", Qt::CaseInsensitive))
            continue;
        for (const auto &hex : attribute(token.Get(), L"Language").split(';')) {
            bool ok = false;
            const auto id = hex.toUInt(&ok, 16);
            if (!ok)
                continue;
            wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
            if (LCIDToLocaleName(id, name, LOCALE_NAME_MAX_LENGTH, 0)) {
                const auto tag = QString::fromWCharArray(name);
                if (!locales.contains(tag))
                    locales.append(tag);
            }
            const bool neutral = !language.contains('-') && !language.contains('_');
            if (!chosen.Get() && (id == requested || (neutral && PRIMARYLANGID(id) == PRIMARYLANGID(requested))))
                chosen = token;
        }
    }
    return chosen;
}
class Recognition : public QObject {
public:
    explicit Recognition(ISpObjectToken *token) {
        require(CoCreateInstance(CLSID_SpInprocRecognizer, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&recognizer_)),
                "Recognizer creation");
        require(recognizer_->SetRecognizer(token), "Recognizer selection");
        SPRECOGNIZERSTATUS status{};
        require(recognizer_->GetStatus(&status), "Recognizer language");
        if (!status.cLangIDs)
            throw QString("The recognizer has no installed language.");
        language_ = status.aLangID[0];
        wchar_t *id = nullptr;
        require(category(SPCAT_AUDIOIN)->GetDefaultTokenId(&id), "Default microphone");
        ComPtr<ISpObjectToken> input;
        const auto created = CoCreateInstance(CLSID_SpObjectToken, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&input));
        if (FAILED(created)) {
            CoTaskMemFree(id);
            require(created, "Microphone token");
        }
        const auto configured = input->SetId(nullptr, id, FALSE);
        CoTaskMemFree(id);
        require(configured, "Microphone lookup");
        require(recognizer_->SetInput(input.Get(), TRUE), "Microphone capture");
        require(recognizer_->CreateRecoContext(&context_), "Recognition context");
        require(context_->SetNotifyWin32Event(), "Recognition events");
        const ULONGLONG interest = SPFEI(SPEI_RECOGNITION) | SPFEI(SPEI_HYPOTHESIS) | SPFEI(SPEI_END_SR_STREAM);
        require(context_->SetInterest(interest, interest), "Recognition event subscription");
        require(context_->CreateGrammar(1, &grammar_), "Script grammar");
        notifier_ = new QWinEventNotifier(context_->GetNotifyEventHandle(), this);
        connect(notifier_, &QWinEventNotifier::activated, this, [this] { events(); });
        require(recognizer_->SetRecoState(SPRST_INACTIVE_WITH_PURGE), "Pause recognition");
    }
    ~Recognition() override {
        if (notifier_)
            notifier_->setEnabled(false);
        if (recognizer_.Get())
            recognizer_->SetRecoState(SPRST_INACTIVE_WITH_PURGE);
    }
    void update(const QJsonObject &command) {
        const auto phrases = command.value("phrases").toArray();
        if (phrases.isEmpty() || phrases.size() > 256)
            throw QString("Add a script before using Windows system speech.");
        require(recognizer_->SetRecoState(SPRST_INACTIVE_WITH_PURGE), "Pause grammar update");
        drain();
        require(grammar_->ResetGrammar(language_), "Reset script grammar");
        SPSTATEHANDLE rule = nullptr;
        require(grammar_->GetRule(L"script", 1, SPRAF_TopLevel | SPRAF_Dynamic, TRUE, &rule), "Create script rule");
        for (const auto &entry : phrases) {
            const auto phrase = entry.toString();
            if (phrase.isEmpty() || phrase.size() > 512)
                throw QString("Invalid script phrase.");
            const auto wide = phrase.toStdWString();
            require(grammar_->AddWordTransition(rule, nullptr, wide.c_str(), L" ", SPWT_LEXICAL, 1.0F, nullptr),
                    "Add script phrase");
        }
        require(grammar_->Commit(0), "Compile script grammar");
        require(grammar_->SetRuleState(L"script", nullptr, SPRS_ACTIVE), "Activate script grammar");
        revision_ = command.value("revision").toInt();
        require(recognizer_->SetRecoState(SPRST_ACTIVE_ALWAYS), "Start microphone");
        if (!started_) {
            started_ = true;
            send({{"type", "ready"}});
        }
    }

private:
    static void clear(SPEVENT &event) {
        if (event.elParamType == SPET_LPARAM_IS_OBJECT && event.lParam)
            reinterpret_cast<IUnknown *>(event.lParam)->Release();
        else if ((event.elParamType == SPET_LPARAM_IS_POINTER || event.elParamType == SPET_LPARAM_IS_STRING) &&
                 event.lParam)
            CoTaskMemFree(reinterpret_cast<void *>(event.lParam));
    }
    void drain() {
        SPEVENT event{};
        ULONG count = 0;
        while (context_->GetEvents(1, &event, &count) == S_OK && count)
            clear(event);
    }
    void events() {
        SPEVENT event{};
        ULONG count = 0;
        while (context_->GetEvents(1, &event, &count) == S_OK && count) {
            if (event.eEventId == SPEI_HYPOTHESIS || event.eEventId == SPEI_RECOGNITION) {
                auto *result = reinterpret_cast<ISpRecoResult *>(event.lParam);
                wchar_t *text = nullptr;
                if (result && SUCCEEDED(result->GetText(SP_GETWHOLEPHRASE, SP_GETWHOLEPHRASE, TRUE, &text, nullptr))) {
                    send({{"type", "transcript"},
                          {"text", QString::fromWCharArray(text)},
                          {"final", event.eEventId == SPEI_RECOGNITION},
                          {"revision", revision_}});
                    CoTaskMemFree(text);
                }
            } else if (event.eEventId == SPEI_END_SR_STREAM && FAILED(HRESULT(event.lParam))) {
                send({{"type", "error"},
                      {"message", "Windows microphone capture ended. Check the selected input device."}});
                QCoreApplication::exit(1);
            }
            clear(event);
        }
    }
    ComPtr<ISpRecognizer> recognizer_;
    ComPtr<ISpRecoContext> context_;
    ComPtr<ISpRecoGrammar> grammar_;
    QWinEventNotifier *notifier_ = nullptr;
    LANGID language_ = 0;
    int revision_ = 0;
    bool started_ = false;
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() != 3)
        return 2;
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) {
        send({{"type", "error"}, {"message", "Cannot initialize Windows speech."}});
        return 1;
    }
    int result = 0;
    try {
        QJsonArray locales;
        auto token = selectRecognizer(args[2], locales);
        if (args[1] == "--check")
            send({{"type", "capabilities"},
                  {"available", (token.Get() != nullptr)},
                  {"installed", (token.Get() != nullptr)},
                  {"supported", (token.Get() != nullptr)},
                  {"locales", locales},
                  {"message", token.Get() ? "Windows local script recognition is ready."
                                          : "Install a matching Microsoft desktop speech recognizer in Windows "
                                            "language settings, or choose Moonshine/Vosk."}});
        else if (args[1] == "--install")
            throw QString(
                "Install the speech language through Windows language settings, then check availability again.");
        else if (args[1] == "--listen") {
            if (!token.Get())
                throw QString("No matching Microsoft local speech recognizer is installed.");
            Recognition recognition(token.Get());
            QByteArray pending;
            QTimer commands;
            QObject::connect(&commands, &QTimer::timeout, &recognition, [&] {
                const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
                DWORD available = 0;
                if (!PeekNamedPipe(input, nullptr, 0, nullptr, &available, nullptr)) {
                    app.quit();
                    return;
                }
                if (!available)
                    return;
                char bytes[4096];
                DWORD received = 0;
                if (!ReadFile(input, bytes, qMin<DWORD>(available, sizeof(bytes)), &received, nullptr)) {
                    app.quit();
                    return;
                }
                pending.append(bytes, int(received));
                try {
                    if (pending.size() > 65536)
                        throw QString("Script update is too large.");
                    while (pending.contains('\n')) {
                        const auto end = pending.indexOf('\n');
                        const auto line = pending.left(end);
                        pending.remove(0, end + 1);
                        QJsonParseError error;
                        const auto doc = QJsonDocument::fromJson(line, &error);
                        if (error.error != QJsonParseError::NoError || !doc.isObject())
                            throw QString("Invalid script update.");
                        recognition.update(doc.object());
                    }
                } catch (const QString &message) {
                    send({{"type", "error"}, {"message", message}});
                    app.exit(1);
                }
            });
            commands.start(50);
            result = app.exec();
        } else
            result = 2;
    } catch (const QString &message) {
        send({{"type", "error"}, {"message", message}});
        result = 1;
    }
    CoUninitialize();
    return result;
}
