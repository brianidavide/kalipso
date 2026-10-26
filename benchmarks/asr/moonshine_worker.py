#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Run one Moonshine tiny-streaming model on a mono 16 kHz PCM WAV.

The WAV is fed in 100 ms chunks. By default input is unpaced for throughput;
--realtime paces input to the recording clock for latency measurements.
Event wall times include inference and feeding; model loading is separate.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
import wave
from pathlib import Path
from typing import Any

import numpy as np


SUPPORTED_LANGUAGES = {"en", "de", "es", "ja", "zh"}
SAMPLE_RATE = 16_000
CHUNK_SECONDS = 0.1


def read_pcm16_mono_wav(path: Path) -> np.ndarray:
    with wave.open(str(path), "rb") as wav:
        if wav.getcomptype() != "NONE":
            raise ValueError("audio must be uncompressed PCM WAV")
        if wav.getnchannels() != 1:
            raise ValueError(f"audio must be mono; got {wav.getnchannels()} channels")
        if wav.getframerate() != SAMPLE_RATE:
            raise ValueError(f"audio must be 16000 Hz; got {wav.getframerate()} Hz")
        if wav.getsampwidth() != 2:
            raise ValueError(f"audio must be PCM16; got {wav.getsampwidth() * 8}-bit")
        frame_count = wav.getnframes()
        raw = wav.readframes(frame_count)

    samples = np.frombuffer(raw, dtype="<i2").astype(np.float32)
    samples *= 1.0 / 32768.0
    if samples.size == 0:
        raise ValueError("audio contains no samples")
    return samples


def event_name(event: Any) -> str:
    return type(event).__name__


def run(args: argparse.Namespace) -> dict[str, Any]:
    if args.language not in SUPPORTED_LANGUAGES:
        raise ValueError(
            f"language must be one of: {', '.join(sorted(SUPPORTED_LANGUAGES))}"
        )

    model_dir = args.model_dir.resolve()
    required = ("adapter.ort", "cross_kv.ort", "decoder_kv.ort", "encoder.ort", "streaming_config.json")
    missing = [name for name in required if not (model_dir / name).is_file()]
    if missing:
        raise ValueError(f"model directory is missing required files: {', '.join(missing)}")

    samples = read_pcm16_mono_wav(args.audio)
    audio_duration = samples.size / SAMPLE_RATE

    from moonshine_voice.moonshine_api import ModelArch
    from moonshine_voice.transcriber import (
        Error,
        LineCompleted,
        LineStarted,
        LineTextChanged,
        LineUpdated,
        Transcriber,
    )

    # Moonshine documents this higher guard for non-Latin tokenizers.
    options = {"max_tokens_per_second": 13.0} if args.language in {"ja", "zh"} else None
    load_start = time.monotonic()
    transcriber = Transcriber(
        model_path=model_dir,
        model_arch=ModelArch.TINY_STREAMING,
        update_interval=args.update_interval,
        options=options,
    )

    events: list[dict[str, Any]] = []
    fed_audio_seconds = 0.0
    stream = transcriber.create_stream(update_interval=args.update_interval)
    model_load_ms = (time.monotonic() - load_start) * 1000.0
    decode_start = time.monotonic()
    spent = 0.0
    call_start = decode_start

    def on_event(event: Any) -> None:
        nonlocal events
        line = getattr(event, "line", None)
        current_wall_ms = (time.monotonic() - decode_start) * 1000.0
        line_audio_seconds = None
        line_text = None
        line_id = None
        complete = None
        if line is not None:
            line_audio_seconds = float(line.start_time + line.duration)
            line_text = line.text
            line_id = str(int(line.line_id))  # uint64 IDs must survive JavaScript JSON parsing
            complete = bool(line.is_complete)

        event_type = event_name(event)
        # Retain text updates, line boundaries, completed lines, and errors.
        if event_type not in {
            "LineStarted",
            "LineUpdated",
            "LineTextChanged",
            "LineCompleted",
            "Error",
        }:
            return
        events.append(
            {
                "type": event_type,
                "text": line_text,
                "lineId": line_id,
                "isComplete": complete,
                "audioSeconds": line_audio_seconds,
                "inputAudioSeconds": fed_audio_seconds,
                "decodeCumulativeWallMs": current_wall_ms,
                "decodeMs": (spent + time.monotonic() - call_start) * 1000.0,
                "wallMs": current_wall_ms,
                "error": str(event.error) if isinstance(event, Error) else None,
            }
        )

    stream.add_listener(on_event)
    chunk_samples = max(1, round(CHUNK_SECONDS * SAMPLE_RATE))
    final_transcript = None
    try:
        stream.start()
        spent += time.monotonic() - call_start
        for offset in range(0, samples.size, chunk_samples):
            chunk = samples[offset : offset + chunk_samples]
            fed_audio_seconds = min((offset + chunk.size) / SAMPLE_RATE, audio_duration)
            if args.realtime:
                time.sleep(max(0, fed_audio_seconds - (time.monotonic() - decode_start)))
            call_start = time.monotonic()
            stream.add_audio(chunk.tolist(), SAMPLE_RATE)
            spent += time.monotonic() - call_start
        call_start = time.monotonic()
        final_transcript = stream.stop()
        spent += time.monotonic() - call_start
        decode_wall_ms = (time.monotonic() - decode_start) * 1000.0
    finally:
        transcriber.close()

    if final_transcript is None or any(event.get("error") for event in events):
        raise RuntimeError("Moonshine did not return a successful final transcript")

    lines = []
    for line in final_transcript.lines if final_transcript is not None else []:
        lines.append(
            {
                "text": line.text,
                "startSeconds": float(line.start_time),
                "durationSeconds": float(line.duration),
                "isComplete": bool(line.is_complete),
            }
        )

    return {
        "schemaVersion": 1,
        "engine": "moonshine-voice",
        "engineVersion": "0.1.5",
        "language": args.language,
        "modelArch": "tiny-streaming",
        "modelDir": str(model_dir),
        "audio": str(args.audio.resolve()),
        "sampleRate": SAMPLE_RATE,
        "audioDurationSeconds": audio_duration,
        "modelLoadMs": model_load_ms,
        "decodeWallMs": decode_wall_ms,
        "decodeMs": spent * 1000.0,
        "audioSeconds": audio_duration,
        "realtimeFactor": (decode_wall_ms / 1000.0) / audio_duration,
        "feedMode": "paced-100ms" if args.realtime else "offline-fast-chunks-100ms-no-pacing",
        "events": events,
        "lines": lines,
        "transcript": "\n".join(line["text"] for line in lines if line["text"]),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model-dir", required=True, type=Path)
    parser.add_argument("--language", required=True, choices=sorted(SUPPORTED_LANGUAGES))
    parser.add_argument("--audio", required=True, type=Path)
    parser.add_argument("--realtime", action="store_true", help="Feed audio at recording speed")
    parser.add_argument(
        "--output",
        default="JSON",
        choices=("JSON", "json"),
        help="Output format. Currently JSON is the only supported format.",
    )
    parser.add_argument(
        "--output-file",
        type=Path,
        help="Write the JSON object here instead of stdout.",
    )
    parser.add_argument(
        "--update-interval",
        type=float,
        default=0.5,
        help="Minimum audio seconds between streaming updates (default: 0.5).",
    )
    args = parser.parse_args()

    if args.update_interval <= 0:
        parser.error("--update-interval must be positive")
    if not args.audio.is_file():
        parser.error(f"audio file not found: {args.audio}")
    if not args.model_dir.is_dir():
        parser.error(f"model directory not found: {args.model_dir}")

    try:
        result = run(args)
    except Exception as exc:
        print(f"moonshine_worker: {type(exc).__name__}: {exc}", file=sys.stderr)
        return 1

    output = json.dumps(result, ensure_ascii=False, separators=(",", ":")) + "\n"
    if args.output_file:
        args.output_file.parent.mkdir(parents=True, exist_ok=True)
        args.output_file.write_text(output, encoding="utf-8")
    else:
        sys.stdout.write(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
