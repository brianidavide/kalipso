// SPDX-License-Identifier: AGPL-3.0-or-later
import Foundation
import AVFoundation
import Speech
import Darwin

private let outputLock = NSLock()
private func send(_ event: [String: Any]) {
    guard let data = try? JSONSerialization.data(withJSONObject: event) else { return }
    outputLock.lock()
    FileHandle.standardOutput.write(data)
    FileHandle.standardOutput.write(Data([10]))
    outputLock.unlock()
}
private struct SpeechFailure: Error, LocalizedError {
    let message: String
    var errorDescription: String? { message }
}
@available(macOS 26.0, *)
private final class AudioFeed: @unchecked Sendable {
    let converter: AVAudioConverter
    let format: AVAudioFormat
    let continuation: AsyncStream<AnalyzerInput>.Continuation
    init(input: AVAudioFormat, output: AVAudioFormat, continuation: AsyncStream<AnalyzerInput>.Continuation) throws {
        guard let converter = AVAudioConverter(from: input, to: output) else {
            throw SpeechFailure(message: "Cannot convert this microphone's audio format.")
        }
        self.converter = converter
        self.format = output
        self.continuation = continuation
    }
    func append(_ source: AVAudioPCMBuffer) {
        let capacity = AVAudioFrameCount(ceil(Double(source.frameLength) * format.sampleRate / source.format.sampleRate)) + 64
        guard let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: capacity) else { return }
        var supplied = false
        var error: NSError?
        let status = converter.convert(to: buffer, error: &error) { _, state in
            if supplied { state.pointee = .noDataNow; return nil }
            supplied = true
            state.pointee = .haveData
            return source
        }
        if status == .error {
            send(["type": "error", "message": error?.localizedDescription ?? "Audio conversion failed."])
            exit(1)
        }
        if buffer.frameLength > 0 {
            if case .dropped = continuation.yield(AnalyzerInput(buffer: buffer)) {
                send(["type": "error", "message": "Recognition cannot keep up. Listening stopped."])
                exit(1)
            }
        }
    }
}
@main
struct SystemSpeech {
    static func main() async {
        do {
            guard CommandLine.arguments.count == 3 else { exit(2) }
            guard #available(macOS 26.0, *) else {
                throw SpeechFailure(message: "Apple system speech requires macOS 26 or later. Choose Moonshine or Vosk on older systems.")
            }
            try await run(operation: CommandLine.arguments[1], language: CommandLine.arguments[2])
        } catch {
            send(["type": "error", "message": error.localizedDescription])
            exit(1)
        }
    }
    @available(macOS 26.0, *)
    static func run(operation: String, language: String) async throws {
        let supported = await SpeechTranscriber.supportedLocales
        let locale = await SpeechTranscriber.supportedLocale(equivalentTo: Locale(identifier: language))
        let installed = await SpeechTranscriber.installedLocales
        let hardware = SpeechTranscriber.isAvailable
        let hasAssets = locale.map { selected in installed.contains { $0.identifier == selected.identifier } } ?? false
        if operation == "--check" {
            send(["type": "capabilities", "supported": hardware && locale != nil,
                  "installed": hasAssets, "available": hardware && locale != nil && hasAssets,
                  "locales": supported.map { $0.identifier.replacingOccurrences(of: "_", with: "-") },
                  "message": !hardware ? "Apple speech is unavailable on this device." :
                    locale == nil ? "This language is not supported by Apple speech." :
                    hasAssets ? "Apple on-device speech is ready." : "Install this system speech language before listening."])
            return
        }
        guard hardware, let locale else { throw SpeechFailure(message: "Apple on-device speech does not support this device or language.") }
        let transcriber = SpeechTranscriber(locale: locale, transcriptionOptions: [],
                                            reportingOptions: [.volatileResults], attributeOptions: [.audioTimeRange])
        if operation == "--install" {
            if let request = try await AssetInventory.assetInstallationRequest(supporting: [transcriber]) {
                send(["type": "status", "message": "Downloading the Apple speech language…"])
                try await request.downloadAndInstall()
            }
            send(["type": "complete"])
            return
        }
        guard operation == "--listen" else { exit(2) }
        guard hasAssets else { throw SpeechFailure(message: "Install this system speech language first. Listening does not download models.") }
        let allowed = await AVCaptureDevice.requestAccess(for: .audio)
        guard allowed else { throw SpeechFailure(message: "Microphone access was denied. Enable Kalipso in System Settings → Privacy & Security → Microphone.") }
        DispatchQueue(label: "kalipso.parent").async {
            while readLine() != nil {}
            exit(0)
        }
        let analyzer = SpeechAnalyzer(modules: [transcriber])
        guard let format = await SpeechAnalyzer.bestAvailableAudioFormat(compatibleWith: [transcriber]) else {
            throw SpeechFailure(message: "No supported Apple speech audio format is available.")
        }
        let (stream, continuation) = AsyncStream<AnalyzerInput>.makeStream(bufferingPolicy: .bufferingNewest(8))
        let audio = AVAudioEngine()
        let input = audio.inputNode
        let inputFormat = input.outputFormat(forBus: 0)
        guard inputFormat.sampleRate > 0, inputFormat.channelCount > 0 else { throw SpeechFailure(message: "No microphone is available.") }
        let feed = try AudioFeed(input: inputFormat, output: format, continuation: continuation)
        input.installTap(onBus: 0, bufferSize: 4096, format: inputFormat) { buffer, _ in feed.append(buffer) }
        defer { audio.stop(); input.removeTap(onBus: 0); continuation.finish() }
        let results = Task {
            do {
                for try await result in transcriber.results {
                    send(["type": "transcript", "text": String(result.text.characters), "final": result.isFinal])
                }
            } catch {
                send(["type": "error", "message": error.localizedDescription])
                exit(1)
            }
        }
        defer { results.cancel() }
        audio.prepare()
        try audio.start()
        send(["type": "ready"])
        try await analyzer.start(inputSequence: stream)
        await results.value
    }
}
