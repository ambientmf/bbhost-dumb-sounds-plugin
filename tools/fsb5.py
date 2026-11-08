#!/usr/bin/env python3
"""FMOD FSB5 sound banks (the game's sound/*.fsb).

  fsb5.py list BANK [FILTER]          the samples: index, name, rate, channels, length
  fsb5.py extract BANK NAME OUT.wav   one sample as a WAV (ATRAC9 decoded with at9dec)
  fsb5.py extract-all BANK OUT_DIR    every sample as OUT_DIR/<index>-<name>.wav
  fsb5.py rebuild BANK OUT [NAME=FILE.wav ...]
                                      the bank written again as 16-bit PCM, every
                                      sample decoded, the named ones replaced by WAVs

A bank holds one codec for all its samples; the game's are ATRAC9 (mode 13).
ATRAC9 has no free encoder, so a bank with a replaced sample is all PCM.
at9dec is tools/at9dec.c built by tools/build-at9dec.sh ($AT9DEC names another).
"""
import os
import struct
import subprocess
import sys
import wave

MODE_PCM16 = 2
MODE_AT9 = 13
RATES = [4000, 8000, 11000, 11025, 16000, 22050, 24000, 32000, 44100, 48000, 96000]
CHANNELS = [1, 2, 6, 8]
CHUNK_CHANNELS, CHUNK_RATE, CHUNK_LOOP, CHUNK_AT9 = 1, 2, 3, 9
HERE = os.path.dirname(os.path.abspath(__file__))


class Sample:
    def __init__(self):
        self.name = ""
        self.rate = 0
        self.channels = 0
        self.frames = 0      # samples per channel
        self.offset = 0      # in the data section
        self.size = 0
        self.chunks = []     # [(type, bytes)] in order


class Bank:
    def __init__(self, path):
        with open(path, "rb") as f:
            b = f.read()
        if b[:4] != b"FSB5":
            raise SystemExit(f"{path}: not an FSB5 bank")
        (self.version, n, shdr, names, data, self.mode) = struct.unpack_from("<6I", b, 4)
        self.header_size = 0x3C if self.version == 1 else 0x40
        self.header_tail = b[0x1C:self.header_size]  # flags, hash, guid: kept as they are
        self.samples = []
        p = self.header_size
        for _ in range(n):
            s = Sample()
            (mode,) = struct.unpack_from("<Q", b, p)
            p += 8
            extra = mode & 1
            s.rate = RATES[(mode >> 1) & 0xF]
            s.channels = CHANNELS[(mode >> 5) & 3]
            s.offset = ((mode >> 7) & 0x07FFFFFF) << 5
            s.frames = (mode >> 34) & 0x3FFFFFFF
            while extra:
                (c,) = struct.unpack_from("<I", b, p)
                p += 4
                extra = c & 1
                size = (c >> 1) & 0xFFFFFF
                kind = (c >> 25) & 0x7F
                body = b[p:p + size]
                p += size
                s.chunks.append((kind, body))
                if kind == CHUNK_CHANNELS:
                    s.channels = body[0]
                elif kind == CHUNK_RATE:
                    (s.rate,) = struct.unpack_from("<I", body)
            self.samples.append(s)
        nt = self.header_size + shdr
        if names:
            for i, s in enumerate(self.samples):
                (o,) = struct.unpack_from("<I", b, nt + 4 * i)
                end = b.index(b"\0", nt + o)
                s.name = b[nt + o:end].decode("ascii", "replace")
        ds = nt + names
        for i, s in enumerate(self.samples):
            nxt = self.samples[i + 1].offset if i + 1 < len(self.samples) else data
            s.size = nxt - s.offset
            s.data = b[ds + s.offset:ds + nxt]

    def find(self, name):
        hits = [s for s in self.samples if s.name == name]
        if len(hits) != 1:
            raise SystemExit(f"{len(hits)} samples named {name!r}")
        return hits[0]


def at9_config(s):
    for kind, body in s.chunks:
        if kind == CHUNK_AT9:
            # 4 bytes of config; 8-channel streams carry a word ahead of it
            return body[4:8] if len(body) >= 8 else body[:4]
    raise SystemExit(f"{s.name}: no ATRAC9 config chunk")


def decode(bank, s):
    """The sample as interleaved 16-bit PCM bytes, s.frames long."""
    if bank.mode == MODE_PCM16:
        return s.data[:s.frames * s.channels * 2]
    if bank.mode != MODE_AT9:
        raise SystemExit(f"mode {bank.mode}: only ATRAC9 and PCM16 banks")
    exe = os.environ.get("AT9DEC") or os.path.join(HERE, "..", "build", "at9dec")
    r = subprocess.run([exe, at9_config(s).hex(), str(s.frames)], input=s.data,
                       stdout=subprocess.PIPE, check=True)
    return r.stdout


def write_wav(path, s, pcm):
    with wave.open(path, "wb") as w:
        w.setnchannels(s.channels)
        w.setsampwidth(2)
        w.setframerate(s.rate)
        w.writeframes(pcm)


def read_wav(path, rate, channels):
    with wave.open(path, "rb") as w:
        if w.getsampwidth() != 2 or w.getframerate() != rate or w.getnchannels() != channels:
            raise SystemExit(f"{path}: needs 16-bit {rate} Hz {channels} ch "
                             f"(is {8 * w.getsampwidth()}-bit {w.getframerate()} Hz {w.getnchannels()} ch)")
        return w.readframes(w.getnframes()), w.getnframes()


def write_bank(bank, out, pcm):
    """bank's samples as PCM16: pcm[i] = (bytes, frames, replaced)."""
    shdr = bytearray()
    data = bytearray()
    for s, (body, frames, replaced) in zip(bank.samples, pcm):
        while len(data) % 32:
            data.append(0)
        rate_idx = RATES.index(s.rate) if s.rate in RATES else None
        ch_idx = CHANNELS.index(s.channels) if s.channels in CHANNELS else None
        chunks = [(k, v) for k, v in s.chunks if k == CHUNK_LOOP]
        if replaced:  # a new sound's loop is all of it
            chunks = [(CHUNK_LOOP, struct.pack("<II", 0, frames - 1))] if chunks else []
        if rate_idx is None:
            chunks.insert(0, (CHUNK_RATE, struct.pack("<I", s.rate)))
            rate_idx = 0
        if ch_idx is None:
            chunks.insert(0, (CHUNK_CHANNELS, bytes([s.channels])))
            ch_idx = 0
        mode = (1 if chunks else 0) | (rate_idx << 1) | (ch_idx << 5) | ((len(data) >> 5) << 7) | (frames << 34)
        shdr += struct.pack("<Q", mode)
        for i, (k, v) in enumerate(chunks):
            more = 1 if i + 1 < len(chunks) else 0
            shdr += struct.pack("<I", more | (len(v) << 1) | (k << 25)) + v
        data += body
    while len(data) % 32:
        data.append(0)
    names = bytearray()
    strs = bytearray()
    base = 4 * len(bank.samples)
    for s in bank.samples:
        names += struct.pack("<I", base + len(strs))
        strs += s.name.encode("ascii") + b"\0"
    names += strs
    while len(names) % 16:
        names.append(0)
    head = b"FSB5" + struct.pack("<6I", bank.version, len(bank.samples), len(shdr), len(names), len(data), MODE_PCM16)
    with open(out, "wb") as f:
        f.write(head + bank.header_tail + shdr + names + data)


def main(argv):
    if len(argv) < 3:
        raise SystemExit(__doc__)
    cmd, bank = argv[1], Bank(argv[2])
    if cmd == "list":
        flt = argv[3] if len(argv) > 3 else ""
        print(f"mode {bank.mode}, {len(bank.samples)} samples")
        for i, s in enumerate(bank.samples):
            if flt in s.name:
                kinds = ",".join(str(k) for k, _ in s.chunks)
                print(f"{i:4} {s.name:40} {s.rate:6} Hz {s.channels} ch {s.frames / s.rate:7.3f} s "
                      f"{s.size:8} B  chunks {kinds}")
    elif cmd == "extract":
        s = bank.find(argv[3])
        with wave.open(argv[4], "wb") as w:
            w.setnchannels(s.channels)
            w.setsampwidth(2)
            w.setframerate(s.rate)
            w.writeframes(decode(bank, s))
        print(f"wrote {argv[4]}: {s.name}, {s.rate} Hz {s.channels} ch {s.frames} frames")
    elif cmd == "extract-all":
        out = argv[3]
        os.makedirs(out, exist_ok=True)
        for i, s in enumerate(bank.samples):
            safe = "".join(c if c.isalnum() or c in "-_." else "_" for c in s.name)
            write_wav(os.path.join(out, f"{i:03}-{safe}.wav"), s, decode(bank, s))
            print(f"\r{i + 1}/{len(bank.samples)}", end="", file=sys.stderr)
        print(file=sys.stderr)
        print(f"wrote {len(bank.samples)} samples to {out}")
    elif cmd == "rebuild":
        out = argv[3]
        swaps = dict(a.split("=", 1) for a in argv[4:])
        for name in swaps:
            bank.find(name)
        pcm = []
        for i, s in enumerate(bank.samples):
            if s.name in swaps:
                body, frames = read_wav(swaps[s.name], s.rate, s.channels)
                print(f"replaced {s.name} with {swaps[s.name]} ({frames} frames)")
            else:
                body, frames = decode(bank, s), s.frames
            pcm.append((body, frames, s.name in swaps))
            print(f"\r{i + 1}/{len(bank.samples)}", end="", file=sys.stderr)
        print(file=sys.stderr)
        write_bank(bank, out, pcm)
        print(f"wrote {out}: {os.path.getsize(out)} bytes")
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main(sys.argv)
