#!/usr/bin/env python3
"""Writes an A4 test page as PDF: frame 10 mm from the edge, 100 mm rulers, gray ramp, fine lines."""
import sys

MM = 72 / 25.4
W, H = 595.28, 841.89
content = []


def line(x1, y1, x2, y2, width=0.5):
    content.append(f"{width} w {x1:.2f} {y1:.2f} m {x2:.2f} {y2:.2f} l S")


def text(x, y, size, value):
    content.append(f"BT /F1 {size} Tf {x:.2f} {y:.2f} Td ({value}) Tj ET")


content.append("0 G 0 g")
content.append(f"1 w {10 * MM:.2f} {10 * MM:.2f} {W - 20 * MM:.2f} {H - 20 * MM:.2f} re S")
text(30 * MM, H - 30 * MM, 20, "Samsung ML-1520 test page")
text(30 * MM, H - 38 * MM, 11, "macOS, Apple Silicon, ml1520-print")
text(30 * MM, H - 44 * MM, 9, "Frame: 10 mm from the paper edge. Both rulers must be exactly 100 mm long.")

x0, y0 = 30 * MM, H - 70 * MM
line(x0, y0, x0 + 100 * MM, y0, 1)
for i in range(11):
    tick = (5 if i % 5 else 9) * MM / 2
    line(x0 + i * 10 * MM, y0, x0 + i * 10 * MM, y0 - tick, 0.7)
    text(x0 + i * 10 * MM - 3, y0 - 9 * MM, 7, str(i * 10))

x1, y1 = 30 * MM, H - 190 * MM
line(x1, y1, x1, y1 + 100 * MM, 1)
for i in range(11):
    tick = (5 if i % 5 else 9) * MM / 2
    line(x1, y1 + i * 10 * MM, x1 + tick, y1 + i * 10 * MM, 0.7)
    text(x1 + 6 * MM, y1 + i * 10 * MM - 2, 7, str(i * 10))

gx, gy = 60 * MM, H - 130 * MM
for i in range(11):
    content.append(f"{i / 10:.1f} g {gx + i * 11 * MM:.2f} {gy:.2f} {11 * MM:.2f} {20 * MM:.2f} re f")
content.append("0 g 0 G")
content.append(f"0.5 w {gx:.2f} {gy:.2f} {121 * MM:.2f} {20 * MM:.2f} re S")
text(gx, gy - 5 * MM, 8, "Gray ramp, 0 to 100 % white")

fx, fy = 60 * MM, H - 190 * MM
for i in range(40):
    line(fx, fy, fx + 70 * MM, fy + i * 1.5, 0.25)
text(fx, fy - 5 * MM, 8, "Fine lines, 0.25 pt")

for i, size in enumerate((5, 7, 9, 11, 14, 18)):
    text(30 * MM, (90 - i * 9) * MM, size, f"{size} pt: The quick brown fox jumps over the lazy dog")
text(W / 2 - 20, 14 * MM, 8, "Bottom edge")

stream = "\n".join(content).encode("latin-1")
objects = [
    b"<</Type/Catalog/Pages 2 0 R>>",
    b"<</Type/Pages/Kids[3 0 R]/Count 1>>",
    f"<</Type/Page/Parent 2 0 R/MediaBox[0 0 {W} {H}]/Contents 4 0 R/Resources<</Font<</F1 5 0 R>>>>>>".encode(),
    b"<</Length %d>>\nstream\n" % len(stream) + stream + b"\nendstream",
    b"<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>",
]
pdf, offsets = bytearray(b"%PDF-1.4\n"), []
for number, body in enumerate(objects, 1):
    offsets.append(len(pdf))
    pdf += b"%d 0 obj\n" % number + body + b"\nendobj\n"
xref = len(pdf)
pdf += b"xref\n0 %d\n0000000000 65535 f \n" % (len(objects) + 1)
for offset in offsets:
    pdf += b"%010d 00000 n \n" % offset
pdf += b"trailer<</Size %d/Root 1 0 R>>\nstartxref\n%d\n%%%%EOF\n" % (len(objects) + 1, xref)
with open(sys.argv[1], "wb") as out:
    out.write(pdf)
