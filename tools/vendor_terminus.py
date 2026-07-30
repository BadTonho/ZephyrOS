#!/usr/bin/env python3
"""Gera as faces ASCII da Zephyr UI Bitmap a partir dos BDFs Terminus."""

from __future__ import annotations

import argparse
import hashlib
import sys
from dataclasses import dataclass
from pathlib import Path


ASCII_FIRST = 0x20
ASCII_LAST = 0x7E
GLYPH_COUNT = ASCII_LAST - ASCII_FIRST + 1
FONT_DIR = Path("assets/fonts/terminus")
OUTPUT_PATH = Path("src/drivers/font_data.inc")
LICENSE_SHA256 = "c14f8d795784a547ea35e69c51dee2957bb71a1cdb492ec5321e4b61d3d97630"


@dataclass(frozen=True)
class FaceSpec:
    source: str
    symbol: str
    width: int
    height: int
    sha256: str

    @property
    def row_stride(self) -> int:
        return (self.width + 7) // 8

    @property
    def glyph_stride(self) -> int:
        return self.row_stride * self.height

    @property
    def macro(self) -> str:
        return self.symbol.upper()


FACES = (
    FaceSpec(
        "ter-u16n.bdf",
        "zephyr_ui_8x16",
        8,
        16,
        "5197662b22bf9f3e68d4af9f969a7fefa3edae40dd82ae969a147381130fb4ae",
    ),
    FaceSpec(
        "ter-u20n.bdf",
        "zephyr_ui_10x20",
        10,
        20,
        "b3868699145f51f1c059b955110c35db990d10a07536995cbcf7a4a901f82178",
    ),
    FaceSpec(
        "ter-u24n.bdf",
        "zephyr_ui_12x24",
        12,
        24,
        "ff640e9e097983355e8f70ed8fb645bd850184a0d590de17f6fc9ceec1bf8eaf",
    ),
)


def file_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate_hash(path: Path, expected: str) -> None:
    if not path.is_file():
        raise ValueError(f"arquivo fonte ausente: {path}")
    actual = file_sha256(path)
    if actual != expected:
        raise ValueError(
            f"SHA-256 divergente para {path}: esperado {expected}, obtido {actual}"
        )


def parse_bdf(path: Path, spec: FaceSpec) -> dict[int, bytes]:
    glyphs: dict[int, bytes] = {}
    encoding: int | None = None
    dwidth: tuple[int, int] | None = None
    box: tuple[int, int, int, int] | None = None
    bitmap: list[str] | None = None

    for raw_line in path.read_text(encoding="ascii").splitlines():
        line = raw_line.strip()
        if line.startswith("STARTCHAR "):
            encoding = None
            dwidth = None
            box = None
            bitmap = None
        elif line.startswith("ENCODING "):
            encoding = int(line.split()[1])
        elif line.startswith("DWIDTH "):
            fields = line.split()
            dwidth = (int(fields[1]), int(fields[2]))
        elif line.startswith("BBX "):
            fields = line.split()
            box = tuple(int(value) for value in fields[1:5])
        elif line == "BITMAP":
            bitmap = []
        elif line == "ENDCHAR":
            if encoding is not None and ASCII_FIRST <= encoding <= ASCII_LAST:
                if dwidth != (spec.width, 0):
                    raise ValueError(f"DWIDTH invalido em U+{encoding:04X}")
                if box is None or box[:2] != (spec.width, spec.height):
                    raise ValueError(f"BBX invalido em U+{encoding:04X}")
                if bitmap is None or len(bitmap) != spec.height:
                    raise ValueError(f"altura invalida em U+{encoding:04X}")
                rows = b"".join(bytes.fromhex(row) for row in bitmap)
                if len(rows) != spec.glyph_stride:
                    raise ValueError(f"stride invalido em U+{encoding:04X}")
                padding_mask = (1 << (spec.row_stride * 8 - spec.width)) - 1
                for row in bitmap:
                    if int(row, 16) & padding_mask:
                        raise ValueError(f"padding nao zerado em U+{encoding:04X}")
                glyphs[encoding] = rows
            bitmap = None
        elif bitmap is not None:
            bitmap.append(line)

    expected = set(range(ASCII_FIRST, ASCII_LAST + 1))
    missing = sorted(expected - glyphs.keys())
    if missing:
        raise ValueError(
            "glyphs ASCII ausentes: " + ", ".join(f"U+{code:04X}" for code in missing)
        )
    return glyphs


def c_character_label(codepoint: int) -> str:
    character = chr(codepoint)
    if character == "\\":
        return "\\\\"
    if character == "'":
        return "\\'"
    return character


def render_face(spec: FaceSpec, glyphs: dict[int, bytes]) -> list[str]:
    lines = [
        f"#define {spec.macro}_WIDTH {spec.width}U",
        f"#define {spec.macro}_HEIGHT {spec.height}U",
        f"#define {spec.macro}_ROW_STRIDE {spec.row_stride}U",
        f"#define {spec.macro}_GLYPH_STRIDE {spec.glyph_stride}U",
        "",
        f"static const uint8_t {spec.symbol}[ZEPHYR_UI_GLYPH_COUNT]"
        f"[{spec.macro}_GLYPH_STRIDE] = {{",
    ]
    for codepoint in range(ASCII_FIRST, ASCII_LAST + 1):
        rows = glyphs[codepoint]
        values = ",".join(f"0x{value:02X}" for value in rows)
        label = c_character_label(codepoint)
        lines.append(f"    /* U+{codepoint:04X} '{label}' */ {{{values}}},")
    lines.append("};")
    return lines


def generate(repo: Path) -> str:
    lines = [
        "/* Gerado por tools/vendor_terminus.py; nao edite manualmente.",
        " * Zephyr UI Bitmap deriva de Terminus Font 4.49.1 (OFL-1.1).",
        " * Contem somente U+0020..U+007E, em peso normal.",
        " */",
        "",
        f"#define ZEPHYR_UI_ASCII_FIRST {ASCII_FIRST}U",
        f"#define ZEPHYR_UI_ASCII_LAST {ASCII_LAST}U",
        f"#define ZEPHYR_UI_GLYPH_COUNT {GLYPH_COUNT}U",
        "",
    ]
    validate_hash(repo / FONT_DIR / "OFL.TXT", LICENSE_SHA256)
    for spec in FACES:
        source = repo / FONT_DIR / spec.source
        validate_hash(source, spec.sha256)
        lines.extend(render_face(spec, parse_bdf(source, spec)))
        lines.append("")
    return "\n".join(lines)


def write_output(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="ascii", newline="\n") as output:
        output.write(content)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--check",
        action="store_true",
        help="valida fontes e confirma que o arquivo gerado esta atualizado",
    )
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]

    try:
        generated = generate(repo)
        output = repo / OUTPUT_PATH
        if args.check:
            if not output.is_file() or output.read_text(encoding="ascii") != generated:
                print(f"{OUTPUT_PATH}: dados gerados desatualizados", file=sys.stderr)
                return 1
            print("Terminus Font: fontes e dados gerados OK")
            return 0
        write_output(output, generated)
        print(f"Gerado {OUTPUT_PATH}")
        return 0
    except (OSError, ValueError) as error:
        print(f"Terminus Font: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
