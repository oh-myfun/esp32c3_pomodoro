"""
Generate all Unicode code points for GB2312 character set.

GB2312 encoding maps Chinese characters, symbols, and ASCII into a 94x94 grid.
Each character has a two-byte code: (qu + 0xA0, wei + 0xA0) in EUC-CN encoding.

Zones:
  01-09: Symbols, full-width forms, punctuation, hiragana, katakana, etc.
  10-15: Unused (reserved)
  16-55: Level 1 characters (3755 chars, sorted by pinyin)
  56-87: Level 2 characters (3008 chars, sorted by radical/stroke)

This script converts GB2312 codes to Unicode code points and outputs them
as actual characters suitable for lv_font_conv's --symbols parameter.
"""

import unicodedata


def gb2312_to_unicode(qu, wei):
    """Convert a GB2312 (qu, wei) pair to a Unicode character.

    GB2312 two-byte encoding: byte1 = qu + 0xA0, byte2 = wei + 0xA0.
    We encode to GB2312 bytes then decode to get the Unicode character.
    """
    b1 = qu + 0xA0
    b2 = wei + 0xA0
    gb2312_bytes = bytes([b1, b2])
    try:
        return gb2312_bytes.decode('gb2312')
    except (UnicodeDecodeError, ValueError):
        return None


def generate_gb2312_chars():
    """Generate all GB2312 characters grouped by zone."""
    all_chars = []

    # Basic ASCII (0x20 - 0x7E) -- these are always needed
    ascii_chars = [chr(c) for c in range(0x20, 0x7F)]
    all_chars.extend(ascii_chars)

    # GB2312 zones 01-09: symbols, full-width forms, etc.
    symbol_chars = []
    for qu in range(1, 10):
        for wei in range(1, 95):
            ch = gb2312_to_unicode(qu, wei)
            if ch:
                symbol_chars.append(ch)
    all_chars.extend(symbol_chars)

    # Zone 16-55: Level 1 Chinese characters (3755 total)
    level1_chars = []
    for qu in range(16, 56):
        for wei in range(1, 95):
            ch = gb2312_to_unicode(qu, wei)
            if ch:
                level1_chars.append(ch)
    all_chars.extend(level1_chars)

    # Zone 56-87: Level 2 Chinese characters (3008 total)
    level2_chars = []
    for qu in range(56, 88):
        for wei in range(1, 95):
            ch = gb2312_to_unicode(qu, wei)
            if ch:
                level2_chars.append(ch)
    all_chars.extend(level2_chars)

    return all_chars, len(ascii_chars), len(symbol_chars), len(level1_chars), len(level2_chars)


def main():
    chars, ascii_count, symbol_count, level1_count, level2_count = generate_gb2312_chars()

    # UI symbols (not in GB2312 zones, but used in code)
    ui_symbols = ['↑', '←', '→', '↓', '▂', '▄', '▆', '█',
                  '▲', '▼', '▸', '▾', '●', '✓', '✗', '⇧', '⌫', '↵']
    chars.extend(ui_symbols)

    # Deduplicate while preserving order
    seen = set()
    unique_chars = []
    for ch in chars:
        if ch not in seen:
            seen.add(ch)
            unique_chars.append(ch)

    total = len(unique_chars)

    # Build the symbol string for --symbols parameter
    symbol_string = ''.join(unique_chars)

    # Save to file
    output_path = 'fonts/gb2312_symbols.txt'
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write(symbol_string)

    # Print statistics
    print(f"GB2312 Character Generation Report")
    print(f"===================================")
    print(f"ASCII (0x20-0x7E):      {ascii_count} characters")
    print(f"Symbols (zone 01-09):   {symbol_count} characters")
    print(f"Level 1 (zone 16-55):   {level1_count} characters")
    print(f"Level 2 (zone 56-87):   {level2_count} characters")
    print(f"-----------------------------------")
    print(f"Total (after dedup):    {total} characters")
    print(f"Output saved to:        {output_path}")


if __name__ == '__main__':
    main()
