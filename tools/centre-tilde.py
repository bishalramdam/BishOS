#!/usr/bin/env python3
"""Move a console font's tilde down into the middle of the letter body.

Console fonts draw ~ near the top of the cell. That is typographically
correct -- the tilde began as a spacing diacritic, the mark over n in
"senor" -- but in a shell prompt, where ~ means your home directory and
sits beside ordinary letters, it reads as though it has floated off.

No stock font avoids it at a usable size. Measuring the tilde's vertical
centre across all 374 fonts in kbd-misc and font-terminus: the best
placements (0.44, close to centred) are all 8x16, far too small on a real
monitor, and every font 20px or taller sits between 0.22 and 0.27. Editing
the glyph is the only way to get both a large font and a centred tilde.

    ./centre-tilde.py /usr/share/consolefonts/ter-132b.psf.gz out.psf.gz 9

Install somewhere apk will not overwrite, and load it from the service
table so it applies before the login prompt:

    sudo cp out.psf.gz /usr/local/share/consolefonts/
    consolefont  once  /usr/sbin/setfont /usr/local/share/consolefonts/out.psf.gz
"""
import gzip, struct, sys

TILDE = 0x7E
REFERENCE = 0x41   # 'A' -- its extent is the letter body to centre within


def read_psf(path):
    opener = gzip.open if path.endswith(".gz") else open
    with opener(path, "rb") as f:
        data = f.read()
    if data[:4] != b"\x72\xb5\x4a\x86":
        sys.exit(f"{path}: not a PSF2 font (PSF1 is not handled)")
    _, hdr, _, _, charsize, height, width = struct.unpack("<7I", data[4:32])
    return data, hdr, charsize, height, width


def rows_used(glyph, height, stride):
    return [y for y in range(height)
            if any(glyph[y * stride + b] for b in range(stride))]


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    src, dst, shift = sys.argv[1], sys.argv[2], int(sys.argv[3])

    data, hdr, charsize, height, width = read_psf(src)
    stride = (width + 7) // 8
    body = bytearray(data[hdr:])

    off = TILDE * charsize
    old = bytes(body[off:off + charsize])
    new = bytearray(charsize)
    for y in range(height):
        ny = y + shift
        if 0 <= ny < height:
            for b in range(stride):
                new[ny * stride + b] = old[y * stride + b]

    ref = body[REFERENCE * charsize:(REFERENCE + 1) * charsize]
    print(f"{src}: {width}x{height}")
    print(f"  letter body ('A') spans rows {rows_used(ref, height, stride)[0]}"
          f"-{rows_used(ref, height, stride)[-1]}")
    print(f"  tilde was rows {rows_used(old, height, stride)}")
    after = rows_used(new, height, stride)
    if not after:
        sys.exit("  shift moved the tilde off the cell entirely -- use a smaller one")
    print(f"  tilde now rows {after}")

    body[off:off + charsize] = new
    with gzip.open(dst, "wb") as f:
        f.write(data[:hdr] + bytes(body))
    print(f"  wrote {dst}")


if __name__ == "__main__":
    main()
