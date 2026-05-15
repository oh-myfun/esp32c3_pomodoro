"""Core font generation logic — no GUI dependency.

Pipeline: read symbols → detect/resolve conflicts → write filtered symbol files
→ pass original fonts + filtered symbols to lv_font_conv.
No TTF subsetting needed — lv_font_conv handles symbol filtering directly.
"""

import json
import os
import re
import subprocess
import tempfile


def read_symbols(sym_path):
    """Read a symbol file and return a set of codepoints."""
    cps = set()
    with open(sym_path, "r", encoding="utf-8") as f:
        for ch in f.read():
            cp = ord(ch)
            if cp > 0x001F and cp != 0xFEFF:
                cps.add(cp)
    return cps


def detect_conflicts(font_slots):
    """Detect characters appearing in 2+ fonts.

    Args:
        font_slots: list of {"font": path, "sym": path}

    Returns:
        {codepoint: [slot_indices]}
    """
    char_map = {}
    for i, slot in enumerate(font_slots):
        cps = read_symbols(slot["sym"])
        for cp in cps:
            if cp not in char_map:
                char_map[cp] = []
            char_map[cp].append(i)
    return {cp: indices for cp, indices in char_map.items() if len(indices) >= 2}


def build_filtered_symbols(font_slots, conflicts, resolution):
    """Build per-font filtered symbol strings after conflict resolution.

    Args:
        font_slots: list of {"font": path, "sym": path}
        conflicts: {cp: [slot_indices]} from detect_conflicts
        resolution: {cp: chosen_slot_index}

    Returns:
        list of {"font": path, "symbols_str": filtered_string}
    """
    result = []
    for i, slot in enumerate(font_slots):
        cps = read_symbols(slot["sym"])
        filtered = set()
        for cp in cps:
            if cp in conflicts:
                chosen = resolution.get(cp)
                if chosen is None or chosen != i:
                    continue
            filtered.add(cp)
        sym_str = "".join(chr(cp) for cp in sorted(filtered))
        result.append({"font": slot["font"], "symbols_str": sym_str})
    return result


def find_lv_font_conv():
    """Find lv_font_conv/lib/convert.js path.

    Tries require.resolve first, falls back to npm root -g.

    Returns:
        Absolute path to convert.js, or None if not found.
    """
    try:
        res = subprocess.run(
            ["node", "-e",
             "console.log(require.resolve('lv_font_conv/lib/convert.js'))"],
            capture_output=True, text=True, timeout=10)
        if res.returncode == 0 and res.stdout.strip():
            path = res.stdout.strip()
            if os.path.isfile(path):
                return path
    except Exception:
        pass

    try:
        res = subprocess.run(
            ["node", "-e",
             "console.log(require('path').join("
             "require('child_process').execSync('npm root -g').toString().trim(),"
             "'lv_font_conv','lib','convert.js'))"],
            capture_output=True, text=True, timeout=10)
        if res.returncode == 0 and res.stdout.strip():
            path = res.stdout.strip()
            if os.path.isfile(path):
                return path
    except Exception:
        pass

    return None


def _js(s):
    """Escape for JS single-quoted string."""
    return s.replace("\\", "\\\\").replace("'", "\\'")


def generate_lvgl(font_paths, symbols_strs, sizes, font_name, out_dir,
                  project_dir, lv_fc_path, log_fn=None):
    """Run lv_font_conv for each size.

    Args:
        font_paths: list of original TTF file paths
        symbols_strs: list of filtered symbol strings (parallel to font_paths)
        sizes: list of pixel sizes
        font_name: base font name (e.g. "custom_font")
        out_dir: output directory for .c files
        project_dir: working directory for node subprocess
        lv_fc_path: path to lv_font_conv/lib/convert.js
        log_fn: optional callback(msg) for progress

    Returns:
        list of generated .c file paths (successful ones)
    """
    import json as _json

    os.makedirs(out_dir, exist_ok=True)
    generated = []

    # Pre-encode symbol strings as JS-safe JSON literals
    sym_jsons = [_json.dumps(s) for s in symbols_strs]

    for size in sizes:
        name = f"{font_name}_{size}"
        output_c = os.path.join(out_dir, f"{name}.c").replace(os.sep, "/")

        if log_fn:
            log_fn(f"Generating {name} ({size}px)...")

        font_args_js = []
        for fp, sym_j in zip(font_paths, sym_jsons):
            fp_js = _js(fp.replace("\\", "/"))
            font_args_js.append(
                f"{{source_path:'{fp_js}',"
                f"source_bin:fs.readFileSync('{fp_js}'),"
                f"ranges:[{{symbols:{sym_j}}}]}}"
            )

        script = "\n".join([
            "const fs=require('fs');",
            f"const convert=require('{_js(lv_fc_path.replace(os.sep, '/'))}');",
            "(async()=>{",
            f"  const r=await convert({{",
            f"    size:{size},bpp:4,format:'lvgl',",
            f"    font:[{','.join(font_args_js)}],",
            f"    output:'{_js(output_c)}',",
            f"    lv_font_name:'{_js(name)}',no_compress:true",
            "  });",
            "  for(const[fn,c] of Object.entries(r)){",
            "    let f=c.replace(/#include \"lvgl\\/lvgl\\.h\"/g,'#include \"lvgl.h\"');",
            f"    const targetLH={size + 6};",
            "    const m1=c.match(/\\.line_height\\s*=\\s*(\\d+)/);",
            "    const m2=c.match(/\\.base_line\\s*=\\s*(\\d+)/);",
            "    if(m1){",
            "      f=f.replace(/(\\.line_height\\s*=\\s*)\\d+/,'$1'+targetLH);",
            "      if(m2){const nb=Math.round(parseInt(m2[1])*targetLH/parseInt(m1[1]));",
            "        f=f.replace(/(\\.base_line\\s*=\\s*)\\d+/,'$1'+nb);}",
            "    }",
            "    fs.writeFileSync(fn,f);",
            "    console.log(`  -> ${require('path').basename(fn)} (${(Buffer.byteLength(f)/1024).toFixed(0)} KB)`);",
            "  }",
            "})().catch(e=>{console.error(e.message);process.exit(1)});",
        ])

        result = subprocess.run(
            ["node"], input=script, capture_output=True, text=True,
            cwd=project_dir, timeout=600)

        if log_fn and result.stdout:
            for line in result.stdout.strip().splitlines():
                log_fn(f"  {line}")

        if result.returncode != 0:
            if log_fn:
                log_fn(f"  ERROR: {result.stderr.strip()}")
        else:
            height_px = None
            try:
                with open(output_c, "r", encoding="utf-8") as cf:
                    content = cf.read()
                m_lh = re.search(r'\.line_height\s*=\s*(\d+)', content)
                if m_lh:
                    height_px = int(m_lh.group(1))
            except Exception:
                pass

            height_str = f", height: {height_px}px" if height_px else ""
            if log_fn:
                log_fn(f"  -> {name}.c OK{height_str}")
            generated.append(output_c)

    return generated


def load_conflict_resolution(path):
    """Load conflict resolution from JSON file.

    Supports two formats:
    1. Flat: {"0xABCD": "font.ttf", ...}
    2. Nested: {"version": 1, "resolution": {"0xABCD": "font.ttf", ...}}

    Returns:
        {codepoint_int: font_filename}
    """
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)

    if isinstance(data, dict) and "resolution" in data:
        raw = data["resolution"]
    else:
        raw = data

    result = {}
    for hex_cp, fname in raw.items():
        try:
            cp = int(hex_cp, 16)
            result[cp] = fname
        except ValueError:
            continue
    return result


def save_conflict_resolution(path, resolution, font_paths):
    """Save conflict resolution to JSON file (flat format)."""
    data = {
        f"0x{cp:04X}": os.path.basename(font_paths[slot])
        for cp, slot in resolution.items()
    }
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)


def run_pipeline(font_slots, sizes, font_name="custom_font",
                 out_dir="main/ui", project_dir=None,
                 conflict_file=None, log_fn=None):
    """Run the complete font generation pipeline.

    Args:
        font_slots: list of {"font": ttf_path, "sym": symbols_txt_path}
        sizes: list of pixel sizes, e.g. [14, 16]
        font_name: base name for output files
        out_dir: output directory (relative to project_dir or absolute)
        project_dir: project root directory (defaults to cwd)
        conflict_file: path to conflict resolution JSON (optional)
        log_fn: optional callback(msg) for progress

    Returns:
        True on success, False on failure
    """
    if project_dir is None:
        project_dir = os.getcwd()

    abs_out_dir = out_dir if os.path.isabs(out_dir) else os.path.join(project_dir, out_dir)

    # Step 1: resolve conflicts and build filtered symbol lists
    if len(font_slots) >= 2:
        conflicts = detect_conflicts(font_slots)
        if conflicts:
            if log_fn:
                log_fn(f"Found {len(conflicts)} conflicting characters")

            if conflict_file and os.path.isfile(conflict_file):
                saved = load_conflict_resolution(conflict_file)
                font_name_to_idx = {
                    os.path.basename(s["font"]): i
                    for i, s in enumerate(font_slots)
                }
                resolution = {}
                for cp, indices in conflicts.items():
                    fname = saved.get(cp)
                    if fname and fname in font_name_to_idx:
                        resolution[cp] = font_name_to_idx[fname]
                    else:
                        resolution[cp] = indices[0]
                resolved = sum(1 for cp in resolution if saved.get(cp) is not None)
                if log_fn:
                    log_fn(f"  Loaded {resolved} resolutions from {os.path.basename(conflict_file)}")
            else:
                resolution = {cp: indices[0] for cp, indices in conflicts.items()}
                if log_fn:
                    log_fn("  Default: first font wins for all conflicts")

            filtered_args = build_filtered_symbols(font_slots, conflicts, resolution)
        else:
            if log_fn:
                log_fn("No conflicts detected")
            filtered_args = build_filtered_symbols(font_slots, {}, {})
    else:
        cps = read_symbols(font_slots[0]["sym"])
        sym_str = "".join(chr(cp) for cp in sorted(cps))
        filtered_args = [{"font": font_slots[0]["font"], "symbols_str": sym_str}]

    for i, fa in enumerate(filtered_args):
        if log_fn:
            log_fn(f"  Font {i + 1}: {os.path.basename(fa['font'])} ({len(fa['symbols_str'])} chars)")

    # Step 2: find lv_font_conv
    lv_fc = find_lv_font_conv()
    if not lv_fc:
        if log_fn:
            log_fn("ERROR: lv_font_conv not found. Run: npm install -g lv_font_conv")
        return False

    # Step 3: generate LVGL fonts
    if log_fn:
        log_fn("")
    font_paths = [fa["font"] for fa in filtered_args]
    symbols_strs = [fa["symbols_str"] for fa in filtered_args]
    generated = generate_lvgl(
        font_paths, symbols_strs, sizes, font_name, abs_out_dir,
        project_dir, lv_fc, log_fn)

    if log_fn:
        log_fn("")
        log_fn(f"Done! Generated {len(generated)} file(s)")

    return len(generated) > 0
