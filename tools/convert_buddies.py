#!/usr/bin/env python3
"""
Convert buddy character data from the claude-desktop-buddy source project
into a C source file (buddy_chars.c) for the ESP32 pomodoro firmware.

Source: D:/Project/AI/claude-desktop-buddy/src/buddies/<name>.cpp
Target: main/buddy/buddy_chars.c

Usage:
    python tools/convert_buddies.py
"""

import re
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_DIR = SCRIPT_DIR.parent
SOURCE_DIR = Path(r"D:\Project\AI\claude-desktop-buddy\src\buddies")
OUTPUT_FILE = PROJECT_DIR / "main" / "buddy" / "buddy_chars.c"

# Species order must match the plan exactly
SPECIES_ORDER = [
    "capybara", "duck", "goose", "blob", "cat", "dragon", "octopus",
    "owl", "penguin", "turtle", "snail", "ghost", "axolotl", "cactus",
    "robot", "rabbit", "mushroom", "chonk",
]

BODY_COLORS = {
    "capybara":  "0xC2A6",
    "duck":      "0xFFE0",
    "goose":     "0xFFFF",
    "blob":      "0x07F0",
    "cat":       "0xC2A6",
    "dragon":    "0xF800",
    "octopus":   "0xA01F",
    "owl":       "0x8430",
    "penguin":   "0x041F",
    "turtle":    "0x07E0",
    "snail":     "0xD8FE",
    "ghost":     "0xFFFF",
    "axolotl":   "0xFB1E",
    "cactus":    "0x07E0",
    "robot":     "0xC618",
    "rabbit":    "0xFFFF",
    "mushroom":  "0xF810",
    "chonk":     "0xFD20",
}

# State names in enum order
STATES = ["sleep", "idle", "busy", "attention", "celebrate", "dizzy", "heart"]
STATE_FUNCS = ["doSleep", "doIdle", "doBusy", "doAttention", "doCelebrate", "doDizzy", "doHeart"]

FRAME_LINES = 5
FRAME_COLS = 12
MAX_ANIM_FRAMES = 10
MAX_SEQ_LEN = 32


# ---------------------------------------------------------------------------
# Parsing helpers
# ---------------------------------------------------------------------------

def extract_c_string_literal(text: str, pos: int) -> tuple[str, int]:
    """Extract a C string literal starting at position pos (must be a '"').
    Returns (unescaped_string_content, next_pos_after_closing_quote).
    Handles escaped characters like \\ within the string.
    """
    assert text[pos] == '"', f"Expected '\"' at position {pos}, got {text[pos]!r}"
    result = []
    i = pos + 1
    while i < len(text):
        ch = text[i]
        if ch == '\\':
            # Escape sequence — take next char literally
            if i + 1 < len(text):
                result.append(text[i + 1])
                i += 2
            else:
                result.append(ch)
                i += 1
        elif ch == '"':
            return (''.join(result), i + 1)
        else:
            result.append(ch)
            i += 1
    # If we get here, the string wasn't terminated — return what we have
    return (''.join(result), i)


def parse_pose_arrays(text: str) -> dict[str, list[str]]:
    """Parse all `static const char* const NAME[5] = { "...", "...", ... };` in text.
    Uses character-level parsing for string literals to avoid regex issues with backslashes.
    """
    result = {}
    # Find the start of each pose array declaration
    decl_pattern = re.compile(r'static\s+const\s+char\*\s+const\s+(\w+)\s*\[\s*5\s*\]\s*=\s*\{')

    for m in decl_pattern.finditer(text):
        name = m.group(1)
        pos = m.end()
        strings = []

        # Skip whitespace, then parse 5 string literals separated by commas
        for _ in range(5):
            # Skip whitespace and commas
            while pos < len(text) and text[pos] in ' \t\n\r,':
                pos += 1
            if pos >= len(text) or text[pos] != '"':
                break
            s, pos = extract_c_string_literal(text, pos)
            strings.append(s)

        if len(strings) == 5:
            result[name] = strings
        # else: malformed, skip

    return result


def parse_p_array(text: str) -> list[str]:
    """Parse `const char* const* P[N] = { POSE1, POSE2, ... };` and return list of pose names."""
    pattern = re.compile(
        r'const\s+char\*\s+const\*\s+P\s*\[\s*\d+\s*\]\s*=\s*\{([^}]+)\}\s*;',
        re.DOTALL,
    )
    m = pattern.search(text)
    if not m:
        return []
    body = m.group(1)
    # Extract identifiers (pose names are ALL_CAPS)
    names = re.findall(r'\b([A-Z][A-Z0-9_]*)\b', body)
    return names


def parse_seq(text: str) -> list[int]:
    """Parse `static const uint8_t SEQ[] = { 0,1,0,1,... };`."""
    pattern = re.compile(
        r'static\s+const\s+uint8_t\s+SEQ\s*\[\s*\]\s*=\s*\{([^}]+)\}\s*;',
        re.DOTALL,
    )
    m = pattern.search(text)
    if not m:
        return []
    body = m.group(1)
    values = [int(x.strip()) for x in body.split(',') if x.strip()]
    return values


def extract_state_function(source: str, func_name: str) -> str | None:
    """Extract the body of a static void doXxx(uint32_t t) { ... } function."""
    pattern = re.compile(
        r'static\s+void\s+' + re.escape(func_name) + r'\s*\(\s*uint32_t\s+\w+\s*\)\s*\{',
    )
    m = pattern.search(source)
    if not m:
        return None

    # Find matching closing brace, skipping string literals and comments
    start = m.end()
    depth = 1
    i = start
    while i < len(source) and depth > 0:
        ch = source[i]
        if ch == '/' and i + 1 < len(source) and source[i + 1] == '/':
            # Line comment — skip to end of line
            while i < len(source) and source[i] != '\n':
                i += 1
        elif ch == '"':
            # String literal — skip to closing quote
            i += 1
            while i < len(source) and source[i] != '"':
                if source[i] == '\\':
                    i += 1  # skip escaped char
                i += 1
            i += 1  # skip closing quote
        elif ch == '{':
            depth += 1
            i += 1
        elif ch == '}':
            depth -= 1
            i += 1
        else:
            i += 1
    return source[start:i - 1]


def normalize_line(line: str, width: int = FRAME_COLS) -> str:
    """Pad or truncate a line to exactly `width` characters."""
    if len(line) < width:
        return line + ' ' * (width - len(line))
    return line[:width]


def parse_species(filepath: Path) -> dict:
    """Parse a single species .cpp file and return structured data."""
    source = filepath.read_text(encoding='utf-8')
    species_name = filepath.stem

    result = {"name": species_name, "states": {}}

    for state_name, func_name in zip(STATES, STATE_FUNCS):
        body = extract_state_function(source, func_name)
        if body is None:
            print(f"  WARNING: {func_name} not found in {filepath.name}", file=sys.stderr)
            result["states"][state_name] = None
            continue

        poses = parse_pose_arrays(body)
        p_names = parse_p_array(body)
        seq = parse_seq(body)

        if not poses or not p_names:
            print(f"  WARNING: incomplete data for {state_name} in {filepath.name}", file=sys.stderr)
            result["states"][state_name] = None
            continue

        # Collect unique poses in P[] order, deduplicating by name.
        # P[] may reference the same pose name multiple times (e.g., cat sleep: LOAF appears twice).
        # Each unique name maps to one frame in our frame array.
        # SEQ indices point into P[], so we remap them through name_to_idx.
        unique_poses = []
        name_to_idx = {}
        for pname in p_names:
            if pname not in name_to_idx:
                name_to_idx[pname] = len(unique_poses)
                unique_poses.append(tuple(normalize_line(l) for l in poses[pname]))

        # Remap SEQ: seq_val -> p_names[seq_val] -> name_to_idx[p_names[seq_val]]
        remapped_seq = []
        for s in seq:
            if s < len(p_names):
                remapped_seq.append(name_to_idx[p_names[s]])
            else:
                print(f"  WARNING: SEQ index {s} out of range in {filepath.name}:{state_name}", file=sys.stderr)

        result["states"][state_name] = {
            "frames": unique_poses,
            "seq": remapped_seq,
            "pose_count": len(p_names),  # original P[] size (including duplicates)
        }

    return result


# ---------------------------------------------------------------------------
# Code generation
# ---------------------------------------------------------------------------

def format_c_string(s: str) -> str:
    """Format a string for C source code (escape backslashes and quotes)."""
    escaped = s.replace('\\', '\\\\').replace('"', '\\"')
    return '"' + escaped + '"'


def generate_c_file(species_data: list[dict]) -> str:
    """Generate the complete buddy_chars.c content."""
    lines = []
    lines.append('#include "buddy_chars.h"')
    lines.append('')

    for sp in species_data:
        name = sp["name"]
        lines.append('/* ============================================================')
        lines.append(f' * {name}')
        lines.append(' * ============================================================')
        lines.append(' */')
        lines.append('')

        for state in STATES:
            sd = sp["states"].get(state)
            if sd is None:
                lines.append(f'static const char *const {name}_{state}[MAX_ANIM_FRAMES][BUDDY_FRAME_LINES] = {{')
                lines.append('    { "            ", "            ", "            ", "            ", "            " },')
                lines.append('};')
                lines.append(f'static const uint8_t {name}_{state}_seq[] = {{ 0 }};')
                lines.append('')
                continue

            frames = sd["frames"]
            seq = sd["seq"]

            # Generate frame array
            lines.append(f'static const char *const {name}_{state}[MAX_ANIM_FRAMES][BUDDY_FRAME_LINES] = {{')
            for frame in frames:
                frame_strs = ', '.join(format_c_string(l) for l in frame)
                lines.append(f'    {{ {frame_strs} }},')
            lines.append('};')

            # Generate SEQ array
            seq_str = ','.join(str(v) for v in seq)
            lines.append(f'static const uint8_t {name}_{state}_seq[] = {{ {seq_str} }};')
            lines.append('')

        lines.append('')

    # Generate species registry
    lines.append('/* ============================================================')
    lines.append(' * Species registry')
    lines.append(' * ============================================================')
    lines.append(' */')
    lines.append('')
    lines.append('const buddy_species_t BUDDY_SPECIES[] = {')

    for sp in species_data:
        name = sp["name"]
        color = BODY_COLORS[name]

        state_frames = ', '.join(f'&{name}_{s}' for s in STATES)
        seq_ptrs = ', '.join(f'{name}_{s}_seq' for s in STATES)
        seq_lens = ', '.join(
            str(len(sp["states"][s]["seq"])) if sp["states"].get(s) else '1'
            for s in STATES
        )
        pose_counts = ', '.join(
            str(sp["states"][s]["pose_count"]) if sp["states"].get(s) else '1'
            for s in STATES
        )

        lines.append('    {')
        lines.append(f'        .name = "{name}",')
        lines.append(f'        .body_color = {color},')
        lines.append(f'        .state_frames = {{ {state_frames} }},')
        lines.append(f'        .seq = {{ {seq_ptrs} }},')
        lines.append(f'        .seq_len = {{ {seq_lens} }},')
        lines.append(f'        .pose_count = {{ {pose_counts} }},')
        lines.append('    },')

    lines.append('};')
    lines.append('const int BUDDY_SPECIES_COUNT = sizeof(BUDDY_SPECIES) / sizeof(BUDDY_SPECIES[0]);')
    lines.append('')

    return '\n'.join(lines)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    if not SOURCE_DIR.exists():
        print(f"ERROR: source directory not found: {SOURCE_DIR}", file=sys.stderr)
        sys.exit(1)

    all_species = []
    for sp_name in SPECIES_ORDER:
        filepath = SOURCE_DIR / f"{sp_name}.cpp"
        if not filepath.exists():
            print(f"ERROR: species file not found: {filepath}", file=sys.stderr)
            sys.exit(1)

        print(f"Parsing {sp_name}...")
        data = parse_species(filepath)
        all_species.append(data)

    # Validate all states parsed successfully
    for sp in all_species:
        for state in STATES:
            if sp["states"].get(state) is None:
                print(f"WARNING: {sp['name']} missing state {state}", file=sys.stderr)

    # Validate frame counts don't exceed MAX_ANIM_FRAMES
    for sp in all_species:
        for state in STATES:
            sd = sp["states"].get(state)
            if sd and len(sd["frames"]) > MAX_ANIM_FRAMES:
                print(f"WARNING: {sp['name']}:{state} has {len(sd['frames'])} unique poses (max {MAX_ANIM_FRAMES})", file=sys.stderr)

    # Validate SEQ lengths don't exceed MAX_SEQ_LEN
    for sp in all_species:
        for state in STATES:
            sd = sp["states"].get(state)
            if sd and len(sd["seq"]) > MAX_SEQ_LEN:
                print(f"WARNING: {sp['name']}:{state} SEQ length {len(sd['seq'])} exceeds MAX_SEQ_LEN ({MAX_SEQ_LEN})", file=sys.stderr)

    # Generate C file
    c_content = generate_c_file(all_species)

    OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT_FILE.write_text(c_content, encoding='utf-8')
    print(f"\nGenerated {OUTPUT_FILE}")

    # Print summary
    total_frames = 0
    total_seq_len = 0
    for sp in all_species:
        for state in STATES:
            sd = sp["states"].get(state)
            if sd:
                total_frames += len(sd["frames"])
                total_seq_len += len(sd["seq"])
    print(f"  Species: {len(all_species)}")
    print(f"  Total frame arrays: {total_frames}")
    print(f"  Total SEQ entries: {total_seq_len}")


if __name__ == '__main__':
    main()
