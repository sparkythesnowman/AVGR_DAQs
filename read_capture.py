#!/usr/bin/env python3
"""Convert capture .bin file(s) written by the central DAQ to .txt.

The PIO packs 4 x 8-bit samples into each 32-bit word, MSB-first
(sample 0 in bits 31:24, sample 1 in bits 23:16, …).  The RP2040 is
little-endian, so in memory those four bytes appear reversed: byte 0
holds sample 3, byte 1 holds sample 2, etc.  The SPI transfer sends
raw memory order, so the central receives the bytes in that reversed
order.

To reconstruct correct sample order we reinterpret every 4-byte group
as a little-endian uint32 and then extract bytes MSB-first.
"""

import struct
import sys
from pathlib import Path

import matplotlib.pyplot as plt


def decode_samples(data: bytes) -> list[int]:
    """Return samples in the correct time order from raw DMA/SPI bytes."""
    n_words = len(data) // 4
    words = struct.unpack(f"<{n_words}I", data[: n_words * 4])
    samples = []
    for w in words:
        samples.append((w >> 24) & 0xFF)
        samples.append((w >> 16) & 0xFF)
        samples.append((w >>  8) & 0xFF)
        samples.append( w        & 0xFF)
    return samples


def read_capture(path):
    p = Path(path)
    out_path = p.with_suffix(".txt")

    with open(p, "rb") as f:
        data = f.read()

    samples = decode_samples(data)

    with open(out_path, "w") as out:
        out.write(f"File: {p.name}  ({len(data)} bytes, {len(samples)} samples)\n\n")
        out.write(f"{'Index':>8}  {'Dec':>4}  {'Hex':>4}  {'Bin':>10}\n")
        out.write("-" * 34 + "\n")
        for i, b in enumerate(samples):
            out.write(f"{i:>8}  {b:>4}  {b:>4x}  {b:>08b}\n")

    print(f"Written: {out_path}")


def plot_capture(path):
    """Plot decoded samples (correct time order) vs sample index."""
    p = Path(path)

    with open(p, "rb") as f:
        data = f.read()

    samples = decode_samples(data)

    fig, ax = plt.subplots(figsize=(12, 4))
    ax.plot(samples, linewidth=0.6, color="steelblue")
    ax.set_xlabel("Sample index")
    ax.set_ylabel("Byte value (0–255)")
    ax.set_title(p.name)
    ax.set_ylim(-5, 260)
    ax.grid(True, linewidth=0.4, alpha=0.6)
    fig.tight_layout()

    out_path = p.with_suffix(".png")
    fig.savefig(out_path, dpi=150)
    print(f"Plot saved: {out_path}")
    plt.show()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python read_capture.py <capture.bin> [capture2.bin ...]")
        sys.exit(1)
    for arg in sys.argv[1:]:
        read_capture(arg)
        plot_capture(arg)
