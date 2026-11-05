#!/usr/bin/env python3
"""Decodes QPDL v1 with compression 0x11 back into bitmaps, checks checksums, writes one PNG per page."""
import struct, sys, zlib

def decompress(data, size):
    uncomp = struct.unpack_from("<I", data, 0)[0]
    table = struct.unpack_from("<64H", data, 4)
    r = 4 + 128
    out = bytearray(data[r:r + uncomp])
    r += uncomp
    while r < len(data):
        c = data[r]
        if c & 0x80:
            b = data[r + 1]
            count = ((c & 0x7F) | ((b & 0xC0) << 1)) + 3
            src = len(out) - table[b & 0x3F]
            for i in range(count):
                out.append(out[src + i])
            r += 2
        else:
            n = c + 1
            out += data[r + 1:r + 1 + n]
            r += 1 + n
    if len(out) != size:
        raise ValueError(f"band size {len(out)} instead of {size}")
    return out

def png(path, w, h, rows):
    def chunk(t, b):
        return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b))
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(bytes(rows))) + chunk(b"IEND", b""))

def main(path, prefix):
    q = open(path, "rb").read()
    marker = b"@PJL ENTER LANGUAGE = QPDL\n"
    p = q.index(marker) + len(marker)
    page_nr = 0
    while q[p] == 0:
        h = q[p:p + 17]
        p += 17
        yres, copies, paper, width, height, src, duplex, version, xres = h[1], h[2] << 8 | h[3], h[4], h[5] << 8 | h[6], h[7] << 8 | h[8], h[9], h[11], h[14], h[16]
        bands, skipped, seen = {}, 0, 0
        while q[p] == 0x0C:
            nr, bw, bh, comp, size = q[p + 1], q[p + 2] << 8 | q[p + 3], q[p + 4] << 8 | q[p + 5], q[p + 6], struct.unpack(">I", q[p + 7:p + 11])[0]
            p += 11
            sig, body = q[p:p + 4], q[p + 4:p + size - 4]
            chk = struct.unpack(">I", q[p + size - 4:p + size])[0]
            if sig != b"\xef\xcd\xab\x09" or (sum(body) + sum(sig)) & 0xFFFFFFFF != chk or comp != 0x11:
                raise ValueError(f"band {nr}: wrong signature or checksum")
            bands[nr] = (bw, bh, decompress(body, bw * bh // 8))
            p += size
        if q[p] != 1:
            raise ValueError(f"missing page end at offset {p}")
        p += 3
        page_nr += 1
        bw = width
        wb = bw // 8
        bh = next(iter(bands.values()))[1] if bands else 128
        S = 6
        pw, ph = (wb * 8) // S, height // S
        bitmap = [bytearray(wb) for _ in range(height)]
        for nr, (w, hh, data) in bands.items():
            for x in range(wb):
                col = data[x * hh:(x + 1) * hh]
                for y in range(hh):
                    row = nr * hh + y
                    if row < height:
                        bitmap[row][x] = col[y] ^ 0xFF
        ink = sum(bin(b).count("1") for row in bitmap for b in row)
        rows = bytearray()
        for y in range(ph):
            rows.append(0)
            for x in range(pw):
                n = 0
                for yy in range(y * S, y * S + S, 2):
                    for xx in range(x * S, x * S + S, 2):
                        n += (bitmap[yy][xx >> 3] >> (7 - (xx & 7))) & 1
                rows.append(255 - int(255 * n / 9))
        png(f"{prefix}-p{page_nr}.png", pw, ph, rows)
        print(f"Page {page_nr}: paper={paper} {width}x{height} res={xres}/{yres} copies={copies} src={src} duplex={duplex} qpdl={version} bands={len(bands)} black={100 * ink / (width * height):.2f}%")
    if q[p:p + 10] != b"\x09\x1b%-12345X":
        raise ValueError("missing PJL end")
    print(f"{page_nr} page(s), structure and checksums ok")

main(sys.argv[1], sys.argv[2])
