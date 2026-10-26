#!/usr/bin/env python3
"""Prepare a deterministic 25-clip FLEURS test sample for ASR benchmarking.

Downloads only the five official FLEURS test archives (the archives are streamed;
only selected clips are retained), using the first five distinct transcription
strings in each pinned TSV. Requires Python's standard library.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import struct
import tarfile
import urllib.request
import wave
from pathlib import Path

DATASET = "google/fleurs"
REVISION = "70bb2e84b976b7e960aa89f1c648e09c59f894dd"
LICENSE = "CC-BY-4.0"
LANGUAGES = {
    "en": "en_us",
    "de": "de_de",
    "es": "es_419",
    "ja": "ja_jp",
    "zh": "cmn_hans_cn",
}
HF = "https://huggingface.co/datasets/google/fleurs/resolve/{revision}/data/{config}/{path}"


def fetch(url: str) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": "asr-benchmark-data-prep/1.0"})
    with urllib.request.urlopen(request, timeout=120) as response:
        return response.read()


def read_first_rows(config: str, count: int = 5) -> list[dict[str, str]]:
    url = HF.format(revision=REVISION, config=config, path="test.tsv")
    text = fetch(url).decode("utf-8")
    rows = []
    seen_references = set()
    for index, fields in enumerate(csv.reader(io.StringIO(text), delimiter="\t")):
        if len(fields) < 3:
            raise ValueError(f"Unexpected TSV row {index + 1} in {config}")
        dataset_id, filename, reference = fields[:3]
        if reference in seen_references:
            continue
        seen_references.add(reference)
        rows.append({"row": index, "dataset_id": dataset_id, "filename": filename, "reference": reference})
        if len(rows) == count:
            return rows
    raise ValueError(f"Only {len(rows)} rows found in {config} test TSV")


def extract_first_rows(config: str, rows: list[dict[str, str]], output_dir: Path) -> None:
    wanted = {row["filename"]: row for row in rows}
    url = HF.format(revision=REVISION, config=config, path="audio/test.tar.gz")
    request = urllib.request.Request(url, headers={"User-Agent": "asr-benchmark-data-prep/1.0"})
    with urllib.request.urlopen(request, timeout=180) as response:
        # Streaming gzip tar avoids storing each complete test archive on disk.
        with tarfile.open(fileobj=response, mode="r|gz") as archive:
            for member in archive:
                basename = Path(member.name).name
                if basename not in wanted or not member.isfile():
                    continue
                stream = archive.extractfile(member)
                if stream is None:
                    raise IOError(f"Cannot read archive member {member.name}")
                row = wanted.pop(basename)
                row["source_member"] = member.name
                row["original_bytes"] = stream.read()
                if not wanted:
                    break
    if wanted:
        raise FileNotFoundError(f"Missing audio members in {config} test archive: {sorted(wanted)}")


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def write_pcm16_mono_16k(source: bytes, destination: Path) -> None:
    """Convert FLEURS RIFF/WAVE float32 mono 16 kHz to signed PCM16 WAV."""
    if source[:4] != b"RIFF" or source[8:12] != b"WAVE":
        raise ValueError("Expected a RIFF/WAVE FLEURS source clip")
    pos = 12
    fmt = None
    audio = None
    while pos + 8 <= len(source):
        tag = source[pos:pos + 4]
        size = struct.unpack_from("<I", source, pos + 4)[0]
        start = pos + 8
        chunk = source[start:start + size]
        if tag == b"fmt ":
            if len(chunk) < 16:
                raise ValueError("Truncated WAV fmt chunk")
            fmt = struct.unpack_from("<HHIIHH", chunk)
        elif tag == b"data":
            audio = chunk
            break
        pos = start + size + (size & 1)
    if fmt is None or audio is None:
        raise ValueError("WAV is missing fmt or data chunk")
    encoding, channels, rate, _byte_rate, block_align, bits = fmt
    if (encoding, channels, rate, block_align, bits) != (3, 1, 16000, 4, 32):
        raise ValueError(
            "Unexpected FLEURS audio format; expected IEEE float32, mono, 16000 Hz, "
            f"got format={encoding}, channels={channels}, rate={rate}, "
            f"block_align={block_align}, bits={bits}"
        )
    if len(audio) % 4:
        raise ValueError("Float32 WAV data size is not sample-aligned")
    frames = bytearray()
    for (sample,) in struct.iter_unpack("<f", audio):
        if not (-1.0 <= sample <= 1.0):
            sample = max(-1.0, min(1.0, sample))
        value = max(-32768, min(32767, round(sample * 32767.0)))
        frames.extend(struct.pack("<h", value))
    with wave.open(str(destination), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(16000)
        wav.writeframes(frames)


def prepare(output: Path) -> Path:
    audio_dir = (output / "audio").resolve()
    audio_dir.mkdir(parents=True, exist_ok=True)
    for lang in LANGUAGES:
        for stale in audio_dir.glob(f"{lang}-*.wav"):
            stale.unlink()
        for stale in audio_dir.glob(f".{lang}-*-source.wav"):
            stale.unlink()
    manifest_path = output / "manifest.json"
    records = []
    for lang, config in LANGUAGES.items():
        rows = read_first_rows(config)
        extract_first_rows(config, rows, audio_dir)
        for row in rows:
            source_bytes = row.pop("original_bytes")
            source_member = row.pop("source_member")
            source_path = audio_dir / f".{lang}-row-{row['row']:04d}-source.wav"
            wav_path = (audio_dir / f"{lang}-row-{row['row']:04d}.wav").resolve()
            source_path.write_bytes(source_bytes)
            try:
                write_pcm16_mono_16k(source_bytes, wav_path)
            finally:
                source_path.unlink(missing_ok=True)
            with wave.open(str(wav_path), "rb") as wav:
                params = wav.getparams()
                if params.nchannels != 1 or params.sampwidth != 2 or params.framerate != 16000:
                    raise ValueError(f"Unexpected normalized WAV parameters: {params}")
                duration = params.nframes / params.framerate
            records.append({
                "language": lang,
                "config": config,
                "id": f"{config}:test:row-{row['row']}",
                "absoluteWav": str(wav_path),
                "reference": row["reference"],
                "duration": round(duration, 6),
                "sha256": sha256(wav_path),
                "dataset": DATASET,
                "split": "test",
                "row": row["row"],
                "datasetId": row["dataset_id"],
                "source": {
                    "tsv": HF.format(revision=REVISION, config=config, path="test.tsv"),
                    "archive": HF.format(revision=REVISION, config=config, path="audio/test.tar.gz"),
                    "archiveMember": source_member,
                    "revision": REVISION,
                },
                "license": LICENSE,
                "audioFormat": "WAV PCM signed 16-bit, mono, 16000 Hz",
            })
    manifest = {
        "dataset": DATASET,
        "revision": REVISION,
        "license": LICENSE,
        "selection": "first 5 distinct raw transcription strings per language, preserving official test.tsv order and taking the first row for each exact string; no ASR-based filtering",
        "languages": LANGUAGES,
        "sampleCount": len(records),
        "audioDirectory": str(audio_dir),
        "records": records,
    }
    manifest_path = manifest_path.resolve()
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return manifest_path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("/tmp/kalipso-asr-benchmark"))
    args = parser.parse_args()
    print(prepare(args.output))


if __name__ == "__main__":
    main()
