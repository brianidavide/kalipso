// SPDX-License-Identifier: AGPL-3.0-or-later
import Foundation
import AVFoundation
import Speech
import ScreenCaptureKit
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
            if CommandLine.arguments[1] == "--colors" { try await colors(first: CommandLine.arguments[2]); return }
            try await run(operation: CommandLine.arguments[1], language: CommandLine.arguments[2])
        } catch {
            send(["type": "error", "message": error.localizedDescription])
            exit(1)
        }
    }
    @available(macOS 26.0, *)
    static func colors(first: String) async throws {
        var request: String? = first
        while let line = request {
            guard let data = line.data(using: .utf8), data.count < 1024,
                  let area = try JSONSerialization.jsonObject(with: data) as? [String: Double],
                  let x = area["x"], let y = area["y"], let width = area["width"], let height = area["height"],
                  width > 0, height > 0, width < 20000, height < 20000 else {
                throw SpeechFailure(message: "Invalid reader position.")
            }
            let content = try await SCShareableContent.excludingDesktopWindows(false, onScreenWindowsOnly: true)
            let rect = CGRect(x: x, y: y, width: width, height: height)
            guard let display = content.displays.first(where: { $0.frame.contains(CGPoint(x: rect.midX, y: rect.midY)) }) else {
                throw SpeechFailure(message: "The reader's display is unavailable.")
            }
            let windows = content.windows.filter { $0.owningApplication?.processID == getppid() && $0.title == "Kalipso — reading" }
            guard !windows.isEmpty else { throw SpeechFailure(message: "Cannot exclude Kalipso's reader from screen color sampling.") }
            let filter = SCContentFilter(display: display, excludingWindows: windows)
            let config = SCStreamConfiguration()
            config.sourceRect = rect.intersection(display.frame).offsetBy(dx: -display.frame.minX, dy: -display.frame.minY)
            config.width = 32; config.height = 16; config.showsCursor = false
            let image = try await SCScreenshotManager.captureImage(contentFilter: filter, configuration: config)
            var pixels = [UInt8](repeating: 0, count: 32 * 16 * 4)
            guard let space = CGColorSpace(name: CGColorSpace.sRGB) else { throw SpeechFailure(message: "No sRGB color space.") }
            let value: String? = pixels.withUnsafeMutableBytes { bytes in
                guard let context = CGContext(data: bytes.baseAddress, width: 32, height: 16, bitsPerComponent: 8,
                    bytesPerRow: 128, space: space, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue | CGBitmapInfo.byteOrder32Big.rawValue) else { return nil }
                context.draw(image, in: CGRect(x: 0, y: 0, width: 32, height: 16))
                let values = bytes.bindMemory(to: UInt8.self)
                var red = 0, green = 0, blue = 0
                for offset in stride(from: 0, to: values.count, by: 4) { red += Int(values[offset]); green += Int(values[offset + 1]); blue += Int(values[offset + 2]) }
                return String(format: "#%02x%02x%02x", red / 512, green / 512, blue / 512)
            }
            guard let value else { throw SpeechFailure(message: "Cannot sample screen colors.") }
            send(["type": "color", "color": value])
            request = readLine()
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
