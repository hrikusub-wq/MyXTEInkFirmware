#!/usr/bin/env python3
"""SVGアイコン(Material Symbols由来)をオフラインで1bppモノクロビットマップに
変換し、Cヘッダ(src/generated/icons_generated.h)として出力するビルド前処理。

実機(ESP32-C3)ではSVGパース・ベクター描画・実行時スケーリングを一切行わない。
サイズ違い(24px/40px)はここでそれぞれ別のビットマップとしてラスタライズする。

依存関係:
    pip install resvg-py Pillow

使い方:
    python scripts/convert_icons.py
"""
import io
from pathlib import Path

import resvg_py
from PIL import Image

ROOT_DIR = Path(__file__).parent.parent
ICONS_DIR = ROOT_DIR / "assets" / "icons"
OUTPUT_HEADER = ROOT_DIR / "src" / "generated" / "icons_generated.h"

# (IconId名, SVGファイル名(拡張子なし))。IconIdの列挙順はこのリストで決まる。
ICON_LIST = [
    ("kFolder", "folder"),
    ("kBook", "book"),
    ("kImportContacts", "import_contacts"),
    ("kDescription", "description"),
    ("kMarkdown", "markdown"),
    ("kSettings", "settings"),
    ("kWifi", "wifi"),
    ("kUpdate", "update"),
    ("kPowerSettingsNew", "power_settings_new"),
    ("kFontDownload", "font_download"),
    ("kBluetooth", "bluetooth"),
    ("kDarkMode", "dark_mode"),
    ("kDeleteSweep", "delete_sweep"),
    ("kBookmark", "bookmark"),
    ("kList", "list"),
    ("kSwapVert", "swap_vert"),
    ("kFormatLineSpacing", "format_line_spacing"),
    ("kPlayArrow", "play_arrow"),
    ("kCheck", "check"),
    ("kChevronBackward", "chevron_backward"),
    ("kChevronForward", "chevron_forward"),
    ("kCloud", "cloud"),
]

# リスト/フッター用の小サイズと、ホームグリッド用の大サイズの2種類。
SIZES = [24, 40]

# このalpha値(0-255)以上を「アイコン本体(黒)」とみなす。
ALPHA_THRESHOLD = 128


def rasterize(svg_path: Path, size: int) -> Image.Image:
    data = resvg_py.svg_to_bytes(svg_path=str(svg_path), width=size, height=size)
    return Image.open(io.BytesIO(bytes(data))).convert("RGBA")


def pack_1bpp(img: Image.Image) -> bytes:
    """0=黒/1=白, MSBが左端, 行方向詰めの1bppビット列にパックする。

    フレームバッファ本体(FrameBufferOps)・MiniFontと同じビット規約。
    """
    w, h = img.size
    width_bytes = (w + 7) // 8
    out = bytearray([0xFF]) * (width_bytes * h)
    pixels = img.load()
    for y in range(h):
        for x in range(w):
            _, _, _, a = pixels[x, y]
            if a >= ALPHA_THRESHOLD:
                byte_index = y * width_bytes + (x >> 3)
                bit = 0x80 >> (x & 7)
                out[byte_index] &= (~bit) & 0xFF
    return bytes(out)


def emit_c_array(name: str, data: bytes) -> str:
    hex_bytes = ", ".join(f"0x{b:02X}" for b in data)
    return f"static const uint8_t {name}[{len(data)}] PROGMEM = {{{hex_bytes}}};"


def main() -> None:
    OUTPUT_HEADER.parent.mkdir(parents=True, exist_ok=True)

    lines = [
        "// このファイルは scripts/convert_icons.py により自動生成されています。",
        "// 手動編集しないでください。SVGを変更した場合はスクリプトを再実行してください。",
        "#pragma once",
        "#include <Arduino.h>",
        "",
        "namespace IconAssets {",
        "",
        "enum class IconId {",
    ]
    for enum_name, _ in ICON_LIST:
        lines.append(f"  {enum_name},")
    lines.append("  kCount,")
    lines.append("};")
    lines.append("")

    data_arrays: dict[int, list[str]] = {size: [] for size in SIZES}
    for enum_name, file_stem in ICON_LIST:
        svg_path = ICONS_DIR / f"{file_stem}.svg"
        if not svg_path.exists():
            raise FileNotFoundError(f"SVG not found: {svg_path}")
        for size in SIZES:
            img = rasterize(svg_path, size)
            packed = pack_1bpp(img)
            array_name = f"ICON_{file_stem.upper()}_{size}"
            lines.append(emit_c_array(array_name, packed))
            data_arrays[size].append(array_name)
        print(f"converted {file_stem} ({', '.join(str(s) for s in SIZES)}px)")

    lines.append("")
    for size in SIZES:
        lines.append(f"static const uint8_t* const ICON_BITMAP_{size}[static_cast<int>(IconId::kCount)] = {{")
        lines.append("    " + ", ".join(data_arrays[size]) + ",")
        lines.append("};")
        lines.append("")

    lines.append("}  // namespace IconAssets")
    lines.append("")

    OUTPUT_HEADER.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {OUTPUT_HEADER} ({OUTPUT_HEADER.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
