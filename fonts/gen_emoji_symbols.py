"""
Extract codepoints from NotoEmoji-Regular.ttf, excluding those already
covered by MapleMono (GB2312 + UI symbols). This prevents NotoEmoji's
basic glyphs from overriding MapleMono's higher-quality versions.
"""

from fontTools.ttLib import TTFont


def main():
    emoji_font_path = "fonts/NotoEmoji-Regular.ttf"
    gb2312_path = "fonts/gb2312_symbols.txt"
    output_path = "fonts/emoji_symbols.txt"

    # Load GB2312 symbol set (MapleMono coverage)
    with open(gb2312_path, "r", encoding="utf-8") as f:
        gb2312_text = f.read()
    gb2312_cps = set(ord(ch) for ch in gb2312_text)

    # Load NotoEmoji cmap
    font = TTFont(emoji_font_path)
    cmap = font.getBestCmap()
    font.close()

    # Filter: only keep codepoints NOT in MapleMono's GB2312 set
    all_cps = sorted(cmap.keys())
    filtered_cps = [cp for cp in all_cps if cp not in gb2312_cps]

    symbol_string = "".join(chr(cp) for cp in filtered_cps)

    with open(output_path, "w", encoding="utf-8") as f:
        f.write(symbol_string)

    removed = len(all_cps) - len(filtered_cps)
    print(f"NotoEmoji Symbol Extraction (filtered)")
    print(f"=======================================")
    print(f"Total codepoints:   {len(all_cps)}")
    print(f"GB2312 overlap:     {removed} (removed)")
    print(f"Unique to NotoEmoji: {len(filtered_cps)}")
    print(f"Range: U+{filtered_cps[0]:04X} - U+{filtered_cps[-1]:04X}")
    print(f"Output: {output_path}")


if __name__ == "__main__":
    main()
