"""CLI entry point for lvfont_tool generate command."""

import argparse
import os
import sys


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="lvfont_tool",
        description="LVGL Font Tool — generate bitmap fonts from TTF files")
    sub = parser.add_subparsers(dest="command")

    # ---- generate ----
    gen = sub.add_parser("generate", help="Generate LVGL font files")
    gen.add_argument(
        "slots", nargs="+",
        help="Pairs of: <font.ttf> <symbols.txt> (at least one pair)")
    gen.add_argument("--conflicts", "-c", help="Conflict resolution JSON file")
    gen.add_argument("--sizes", "-s", default="14,16",
                     help="Comma-separated pixel sizes (default: 14,16)")
    gen.add_argument("--name", "-n", default="custom_font",
                     help="Base font name (default: custom_font)")
    gen.add_argument("--output", "-o", default="main/ui",
                     help="Output directory (default: main/ui)")
    gen.add_argument("--project", "-p", default=None,
                     help="Project root directory (default: auto-detect)")
    gen.add_argument("--dry-run", action="store_true",
                     help="Show what would be generated without running")

    # ---- check-conflicts ----
    chk = sub.add_parser("check", help="Check conflicts between font slots")
    chk.add_argument(
        "slots", nargs="+",
        help="Pairs of: <font.ttf> <symbols.txt>")
    chk.add_argument("--conflicts", "-c", help="Existing conflict resolution JSON")

    args = parser.parse_args(argv)

    if args.command == "generate":
        return _cmd_generate(args)
    elif args.command == "check":
        return _cmd_check(args)
    else:
        parser.print_help()
        return 0


def _parse_slots(raw_slots):
    """Parse positional args into font/symbols pairs."""
    if len(raw_slots) < 2 or len(raw_slots) % 2 != 0:
        print("ERROR: Arguments must be pairs of <font.ttf> <symbols.txt>")
        print("  Example: lvfont_tool generate font1.ttf syms1.txt font2.ttf syms2.txt")
        return None

    pairs = []
    for i in range(0, len(raw_slots), 2):
        font = raw_slots[i]
        sym = raw_slots[i + 1]
        if not os.path.isfile(font):
            print(f"ERROR: Font not found: {font}")
            return None
        if not os.path.isfile(sym):
            print(f"ERROR: Symbols file not found: {sym}")
            return None
        pairs.append({"font": os.path.abspath(font), "sym": os.path.abspath(sym)})
    return pairs


def _cmd_generate(args):
    from .gen_core import read_symbols, detect_conflicts, run_pipeline

    slots = _parse_slots(args.slots)
    if not slots:
        return 1

    try:
        sizes = [int(s.strip()) for s in args.sizes.split(",")]
    except ValueError:
        print(f"ERROR: Invalid sizes: {args.sizes}")
        return 1

    project_dir = args.project
    if not project_dir:
        # Auto-detect: walk up from first font to find project root
        project_dir = os.getcwd()

    def log(msg):
        print(msg)

    print(f"Font generation: {len(slots)} slot(s), sizes {sizes}, name '{args.name}'")
    for i, s in enumerate(slots):
        cps = read_symbols(s["sym"])
        print(f"  Slot {i + 1}: {os.path.basename(s['font'])} + {os.path.basename(s['sym'])} ({len(cps)} chars)")
    print(f"  Output: {args.output}")
    print()

    if args.dry_run:
        print("(dry run — no files generated)")
        if len(slots) >= 2:
            conflicts = detect_conflicts(slots)
            if conflicts:
                print(f"Would need to resolve {len(conflicts)} conflicts")
                if args.conflicts:
                    from .gen_core import load_conflict_resolution
                    res = load_conflict_resolution(args.conflicts)
                    matched = sum(1 for cp in conflicts if cp in res)
                    print(f"  Resolution file covers {matched}/{len(conflicts)} conflicts")
            else:
                print("No conflicts detected")
        return 0

    success = run_pipeline(
        font_slots=slots,
        sizes=sizes,
        font_name=args.name,
        out_dir=args.output,
        project_dir=project_dir,
        conflict_file=args.conflicts,
        log_fn=log,
    )
    return 0 if success else 1


def _cmd_check(args):
    from .gen_core import read_symbols, detect_conflicts, load_conflict_resolution

    slots = _parse_slots(args.slots)
    if not slots:
        return 1

    for i, s in enumerate(slots):
        cps = read_symbols(s["sym"])
        print(f"Slot {i + 1}: {os.path.basename(s['font'])} ({len(cps)} chars)")

    conflicts = detect_conflicts(slots)
    if not conflicts:
        print("\nNo conflicts detected")
        return 0

    print(f"\n{len(conflicts)} conflicting characters:")

    # Group by char for display
    sample = sorted(conflicts.items())[:20]
    for cp, indices in sample:
        char = chr(cp)
        try:
            name = __import__("unicodedata").name(char, "?")
        except Exception:
            name = "?"
        font_names = [os.path.basename(slots[i]["font"]) for i in indices]
        print(f"  U+{cp:04X} ({char}) {name}: {', '.join(font_names)}")
    if len(conflicts) > 20:
        print(f"  ... and {len(conflicts) - 20} more")

    # Check resolution coverage
    if args.conflicts:
        res = load_conflict_resolution(args.conflicts)
        resolved = sum(1 for cp in conflicts if cp in res)
        print(f"\nResolution file: {resolved}/{len(conflicts)} conflicts resolved")
        for cp, indices in sample:
            chosen = res.get(cp)
            if chosen:
                print(f"  U+{cp:04X} -> {chosen}")
            else:
                print(f"  U+{cp:04X} -> (unresolved)")

    return 0
