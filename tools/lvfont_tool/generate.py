"""GenerateMixin: LVGL font generation and conflict resolution dialogs."""

import os
import json
import re
import subprocess
import threading
import unicodedata

import tkinter as tk
from tkinter import filedialog, messagebox

from PIL import Image, ImageDraw, ImageFont, ImageTk

from .i18n import LANG
from .themes import THEMES
from .gen_core import (
    read_symbols, detect_conflicts, build_filtered_symbols,
    find_lv_font_conv, generate_lvgl,
    load_conflict_resolution, save_conflict_resolution,
)


class GenerateMixin:

    def _open_generate_dialog(self):
        dlg = tk.Toplevel(self.root)
        dlg.title(self.t("gen_title"))
        dlg.geometry("640x520")
        dlg.configure(bg=self.c("panel"))

        slots = []
        slot_frames = []
        MAX_SLOTS = 4

        slots_container = tk.Frame(dlg, bg=self.c("panel"))
        slots_container.pack(fill=tk.X, padx=10, pady=4)

        def _browse_font(var):
            p = filedialog.askopenfilename(
                parent=dlg, filetypes=[("Font", "*.ttf *.otf"), ("All", "*.*")])
            if p:
                var.set(p)

        def _browse_sym(var, count_lbl):
            p = filedialog.askopenfilename(
                parent=dlg, filetypes=[("Text", "*.txt"), ("All", "*.*")])
            if p:
                var.set(p)
                _update_sym_count(p, count_lbl)

        def _update_sym_count(sym_path, count_lbl):
            try:
                with open(sym_path, "r", encoding="utf-8") as f:
                    text = f.read()
                count = sum(1 for ch in text if ord(ch) > 0x001F and ord(ch) != 0xFEFF)
                count_lbl.config(text=f"({count})")
            except Exception:
                count_lbl.config(text="")

        def _add_slot(prefill_font=""):
            idx = len(slots)
            if idx >= MAX_SLOTS:
                return

            # Header row: slot label + delete button
            header_fr = tk.Frame(slots_container, bg=self.c("panel"))
            header_fr.pack(fill=tk.X, pady=(4, 0))

            slot_lbl = tk.Label(header_fr, text=f"{self.t('gen_slot')} {idx + 1}",
                                bg=self.c("panel"), fg=self.c("text_dim"),
                                font=("Segoe UI", 9, "bold"))
            slot_lbl.pack(side=tk.LEFT)

            del_btn = tk.Button(header_fr, text="  ×  ",
                                command=lambda i=idx: _remove_slot_by_index(i),
                                bg=self.c("btn_bg"), fg=self.c("btn_fg"), relief=tk.FLAT,
                                font=("Segoe UI", 7))
            del_btn.pack(side=tk.RIGHT, padx=2)

            fr = tk.Frame(slots_container, bg=self.c("panel"), bd=1, relief=tk.GROOVE)
            fr.pack(fill=tk.X, pady=(0, 2), padx=2)

            # Font row
            row_font = tk.Frame(fr, bg=self.c("panel"))
            row_font.pack(fill=tk.X, padx=6, pady=2)
            tk.Label(row_font, text=self.t("gen_font"), bg=self.c("panel"), fg=self.c("btn_fg"),
                     font=("Segoe UI", 8), width=8, anchor=tk.W).pack(side=tk.LEFT)
            font_var = tk.StringVar(value=prefill_font)
            tk.Entry(row_font, textvariable=font_var, bg=self.c("input_bg"),
                     fg=self.c("input_fg"), font=("Consolas", 9), relief=tk.FLAT
                     ).pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(4, 2))
            tk.Button(row_font, text=self.t("gen_browse"),
                      command=lambda v=font_var: _browse_font(v),
                      bg=self.c("btn_bg"), fg=self.c("btn_fg"), relief=tk.FLAT, padx=6,
                      font=("Segoe UI", 7)).pack(side=tk.LEFT)

            # Symbol row
            row_sym = tk.Frame(fr, bg=self.c("panel"))
            row_sym.pack(fill=tk.X, padx=6, pady=(2, 4))
            tk.Label(row_sym, text=self.t("gen_symbols"), bg=self.c("panel"), fg=self.c("btn_fg"),
                     font=("Segoe UI", 8), width=8, anchor=tk.W).pack(side=tk.LEFT)
            sym_var = tk.StringVar()
            tk.Entry(row_sym, textvariable=sym_var, bg=self.c("input_bg"),
                     fg=self.c("input_fg"), font=("Consolas", 9), relief=tk.FLAT
                     ).pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(4, 2))

            count_lbl = tk.Label(row_sym, text="", bg=self.c("panel"), fg=self.c("text_muted"),
                                 font=("Segoe UI", 8))
            count_lbl.pack(side=tk.LEFT, padx=(4, 0))

            tk.Button(row_sym, text=self.t("gen_browse"),
                      command=lambda v=sym_var, cl=count_lbl: _browse_sym(v, cl),
                      bg=self.c("btn_bg"), fg=self.c("btn_fg"), relief=tk.FLAT, padx=6,
                      font=("Segoe UI", 7)).pack(side=tk.LEFT, padx=(4, 0))

            slot_data = {
                "font": font_var, "sym": sym_var, "frame": fr,
                "count_lbl": count_lbl, "header": header_fr, "label": slot_lbl,
                "del_btn": del_btn,
            }
            slots.append(slot_data)
            slot_frames.append(fr)
            _update_conflict_btn_state()

        def _remove_slot_by_index(slot_idx):
            if len(slots) <= 1:
                return
            if slot_idx >= len(slots):
                return
            s = slots[slot_idx]
            s["header"].destroy()
            s["frame"].destroy()
            slots.pop(slot_idx)
            slot_frames.pop(slot_idx)
            # Renumber
            for i, s in enumerate(slots):
                s["label"].config(text=f"{self.t('gen_slot')} {i + 1}")
                s["del_btn"].config(
                    command=lambda idx=i: _remove_slot_by_index(idx))
            _update_conflict_btn_state()

        def _update_conflict_btn_state():
            """Enable/disable conflict button based on slot count."""
            try:
                if len(slots) >= 2:
                    btn_conflict.config(state=tk.NORMAL)
                else:
                    btn_conflict.config(state=tk.DISABLED)
            except NameError:
                pass

        # Create bottom buttons early so _update_conflict_btn_state can reference them
        btn_bottom = tk.Frame(dlg, bg=self.c("panel"))
        btn_bottom.pack(fill=tk.X, padx=10, pady=(0, 8), side=tk.BOTTOM)

        btn_conflict = tk.Button(btn_bottom, text=self.t("gen_conflict_btn"),
                                 command=lambda: _open_conflict_check(),
                                 bg=self.c("btn_bg"), fg=self.c("btn_fg"), relief=tk.FLAT, padx=10,
                                 font=("Segoe UI", 9), state=tk.DISABLED)
        btn_conflict.pack(side=tk.LEFT)

        # Initial 2 slots: first pre-filled with current font, second empty
        _add_slot(prefill_font=self.font_path or "")
        _add_slot()

        # Add slot button
        add_fr = tk.Frame(dlg, bg=self.c("panel"))
        add_fr.pack(fill=tk.X, padx=10)

        btn_add = tk.Button(add_fr, text=self.t("gen_add_slot"),
                            command=lambda: _add_slot(),
                            bg=self.c("btn_bg"), fg=self.c("btn_fg"), relief=tk.FLAT, padx=8,
                            font=("Segoe UI", 8))
        btn_add.pack(side=tk.LEFT)

        # Options
        opt_fr = tk.Frame(dlg, bg=self.c("panel"))
        opt_fr.pack(fill=tk.X, padx=10, pady=4)
        tk.Label(opt_fr, text=self.t("gen_sizes"), bg=self.c("panel"), fg=self.c("btn_fg"),
                 font=("Segoe UI", 8)).pack(side=tk.LEFT)
        sizes_var = tk.StringVar(value="14, 16")
        tk.Entry(opt_fr, textvariable=sizes_var, bg=self.c("input_bg"), fg=self.c("input_fg"),
                 font=("Consolas", 9), width=12, relief=tk.FLAT).pack(side=tk.LEFT, padx=6)

        # Font name
        tk.Label(opt_fr, text=self.t("gen_name"), bg=self.c("panel"), fg=self.c("btn_fg"),
                 font=("Segoe UI", 8)).pack(side=tk.LEFT, padx=(12, 0))
        name_var = tk.StringVar(value="custom_font")
        tk.Entry(opt_fr, textvariable=name_var, bg=self.c("input_bg"), fg=self.c("input_fg"),
                 font=("Consolas", 9), width=14, relief=tk.FLAT).pack(side=tk.LEFT, padx=6)

        # Output dir
        out_var = tk.StringVar(value="main/ui")

        def _browse_outdir():
            d = filedialog.askdirectory(parent=dlg, initialdir=self._project_dir)
            if d:
                try:
                    rel = os.path.relpath(d, self._project_dir)
                    out_var.set(rel.replace("\\", "/"))
                except ValueError:
                    out_var.set(d)

        tk.Label(opt_fr, text=self.t("gen_output"), bg=self.c("panel"), fg=self.c("btn_fg"),
                 font=("Segoe UI", 8)).pack(side=tk.LEFT, padx=(12, 0))
        tk.Entry(opt_fr, textvariable=out_var, bg=self.c("input_bg"), fg=self.c("input_fg"),
                 font=("Consolas", 9), relief=tk.FLAT).pack(side=tk.LEFT, fill=tk.X,
                                                             expand=True, padx=6)
        tk.Button(opt_fr, text=self.t("gen_browse"), command=_browse_outdir,
                  bg=self.c("btn_bg"), fg=self.c("btn_fg"), relief=tk.FLAT, padx=6,
                  font=("Segoe UI", 7)).pack(side=tk.LEFT, padx=(4, 0))

        # Log
        log = tk.Text(dlg, bg=self.c("bg"), fg=self.c("text"), font=("Consolas", 8),
                      height=8, relief=tk.FLAT, highlightthickness=0)
        log.pack(fill=tk.BOTH, expand=True, padx=10, pady=(4, 6))

        def _collect_font_args():
            """Collect filled font slots and read their symbol files."""
            font_args = []
            for slot in slots:
                fp = slot["font"].get().strip()
                sp = slot["sym"].get().strip()
                if fp and sp:
                    font_args.append({"font": fp, "sym": sp, "slot_idx": len(font_args)})
            return font_args

        def _log_sync(msg):
            log.insert(tk.END, msg + "\n")
            log.see(tk.END)

        def _open_conflict_check():
            """Conflict handling button: detect conflicts and open dialog."""
            log.delete("1.0", tk.END)
            if len(slots) < 2:
                log.insert(tk.END, f"{self.t('gen_no_slots')}\n")
                return

            font_args = _collect_font_args()
            if not font_args:
                log.insert(tk.END, "ERROR: No font slots configured\n")
                return

            conflicts = detect_conflicts(font_args)
            if not conflicts:
                log.insert(tk.END, f"{self.t('conflict_no_conflict')}\n")
                return

            font_paths = [fa["font"] for fa in font_args]
            font_name_to_idx = {os.path.basename(fp): i for i, fp in enumerate(font_paths)}
            log.insert(tk.END, f"{len(conflicts)} {self.t('conflict_desc')}\n")
            log.see(tk.END)

            # Build initial resolution: restore from memory if available
            default_resolution = {cp: indices[0] for cp, indices in conflicts.items()}
            saved = getattr(self, '_conflict_resolution', None)
            if saved and isinstance(saved, dict):
                for cp, fname in saved.items():
                    if cp in conflicts and fname in font_name_to_idx:
                        default_resolution[cp] = font_name_to_idx[fname]

            def on_conflict_done(resolution, excluded):
                # Store as {cp: font_filename} for portability
                self._conflict_resolution = {
                    cp: os.path.basename(font_paths[slot])
                    for cp, slot in resolution.items()
                }
                log.insert(tk.END, f"{self.t('gen_conflict_resolved')}\n")
                log.see(tk.END)

            self._open_conflict_dialog(
                dlg, conflicts, font_paths,
                initial_resolution=default_resolution,
                on_done=on_conflict_done, name_var=name_var)

        def _set_ui_locked(locked):
            state = tk.DISABLED if locked else tk.NORMAL
            btn_run.config(state=state)
            btn_conflict.config(state=state)
            btn_add.config(state=state)
            for s in slots:
                s["del_btn"].config(state=state)

        def run_generate():
            log.delete("1.0", tk.END)
            # Check node
            try:
                subprocess.run(["node", "--version"], capture_output=True, check=True)
            except Exception:
                log.insert(tk.END, f"ERROR: {self.t('gen_no_node')}\n")
                return
            # Find lv_font_conv
            lv_fc = find_lv_font_conv()
            if not lv_fc:
                log.insert(tk.END, f"ERROR: {self.t('gen_no_lv_font_conv')}\n")
                return

            try:
                sizes = [int(s.strip()) for s in sizes_var.get().split(",")]
            except ValueError:
                log.insert(tk.END, "ERROR: Invalid sizes\n")
                return

            out_dir = os.path.join(self._project_dir, out_var.get())

            font_args = _collect_font_args()
            if not font_args:
                log.insert(tk.END, "ERROR: No font slots configured\n")
                return

            # Single font: skip conflict detection entirely
            if len(font_args) < 2:
                cps = read_symbols(font_args[0]["sym"])
                sym_str = "".join(chr(cp) for cp in sorted(cps))
                filtered_args = [{"font": font_args[0]["font"], "symbols_str": sym_str}]
            else:
                # Multiple fonts: use saved resolution if available
                conflicts = detect_conflicts(font_args)
                if not conflicts:
                    log.insert(tk.END, f"{self.t('conflict_no_conflict')}\n")
                    filtered_args = build_filtered_symbols(font_args, {}, {})
                else:
                    # Convert memory resolution {cp: filename} → {cp: slot_index}
                    saved = getattr(self, '_conflict_resolution', None)
                    font_name_to_idx = {os.path.basename(fa["font"]): i
                                        for i, fa in enumerate(font_args)}
                    if saved:
                        slot_resolution = {}
                        for cp, fname in saved.items():
                            if cp in conflicts and fname in font_name_to_idx:
                                slot_resolution[cp] = font_name_to_idx[fname]
                        for cp, indices in conflicts.items():
                            if cp not in slot_resolution:
                                slot_resolution[cp] = indices[0]
                    else:
                        slot_resolution = {cp: indices[0] for cp, indices in conflicts.items()}
                    filtered_args = build_filtered_symbols(
                        font_args, conflicts, slot_resolution)

            _set_ui_locked(True)

            def _log(msg):
                dlg.after(0, lambda m=msg: (log.insert(tk.END, m + "\n"), log.see(tk.END)))

            def worker():
                font_name = name_var.get().strip() or "custom_font"

                for i, fa in enumerate(filtered_args):
                    _log(f"  Font {i + 1}: {os.path.basename(fa['font'])} ({len(fa['symbols_str'])} chars)")

                # Generate LVGL fonts directly (no subsetting needed)
                _log("")
                font_paths = [fa["font"] for fa in filtered_args]
                symbols_strs = [fa["symbols_str"] for fa in filtered_args]
                generated = generate_lvgl(
                    font_paths, symbols_strs, sizes, font_name, out_dir,
                    self._project_dir, lv_fc, _log)

                if generated:
                    _log(f"\n{self.t('gen_done')}")
                else:
                    _log("\nGeneration failed")
                dlg.after(0, lambda: _set_ui_locked(False))

            threading.Thread(target=worker, daemon=True).start()

        # Run button (btn_bottom and btn_conflict created earlier)
        btn_run = tk.Button(btn_bottom, text=self.t("gen_run"), command=run_generate,
                            bg=self.c("btn_primary"), fg="white", relief=tk.FLAT, padx=16,
                            font=("Segoe UI", 10, "bold"))
        btn_run.pack(side=tk.RIGHT)

    # ---- Conflict Resolution Dialog ----

    def _open_conflict_dialog(self, parent, conflicts, font_paths,
                              initial_resolution=None, on_done=None, name_var=None):
        """Open conflict resolution dialog with table layout.

        Table: | # | Char | Font1 | Font2 | ...
        Click a font cell to select. Close window to apply.
        """
        conflict_cps = sorted(conflicts.keys())
        total = len(conflict_cps)
        num_fonts = len(font_paths)

        dlg = tk.Toplevel(parent)
        dlg.title(f"{self.t('conflict_title')} ({total})")
        dlg.geometry("860x600")
        dlg.configure(bg=self.c("panel"))
        dlg.transient(parent)
        dlg.grab_set()

        resolution = {}
        if initial_resolution:
            resolution.update(initial_resolution)
        for cp in conflict_cps:
            if cp not in resolution:
                resolution[cp] = conflicts[cp][0]

        CELL_W = 64
        CELL_H = 56
        PREVIEW_SIZE = 36

        def _render_char(char, font_path, size=PREVIEW_SIZE):
            """Render character with Pillow — same approach as main app."""
            try:
                font = ImageFont.truetype(font_path, size)
            except Exception:
                font = ImageFont.load_default()
            img_size = size + 16
            img = Image.new("RGBA", (img_size, img_size), self.c("preview_img_bg"))
            draw = ImageDraw.Draw(img)
            fg = self.c("preview_fg")[:3]
            draw.text((img_size // 2, img_size // 2), char,
                      fill=fg, font=font, anchor="mm")
            return ImageTk.PhotoImage(img)

        # ---- Top bar ----
        top_fr = tk.Frame(dlg, bg=self.c("panel"))
        top_fr.pack(fill=tk.X, padx=10, pady=(8, 4))

        tk.Label(top_fr, text=f"{total} {self.t('conflict_desc')}",
                 bg=self.c("panel"), fg=self.c("text"),
                 font=("Segoe UI", 10, "bold")).pack(side=tk.LEFT)

        status_var = tk.StringVar(value="")
        tk.Label(top_fr, textvariable=status_var, bg=self.c("panel"),
                 fg=self.c("text_muted"), font=("Segoe UI", 8)).pack(side=tk.LEFT, padx=12)

        tk.Button(top_fr, text=self.t("conflict_save"),
                  command=lambda: _save_resolution(),
                  bg=self.c("btn_green"), fg="white", relief=tk.FLAT, padx=8,
                  font=("Segoe UI", 8)).pack(side=tk.RIGHT, padx=(4, 0))
        tk.Button(top_fr, text=self.t("conflict_load"),
                  command=lambda: _load_resolution(),
                  bg=self.c("btn_bg"), fg=self.c("btn_fg"), relief=tk.FLAT, padx=8,
                  font=("Segoe UI", 8)).pack(side=tk.RIGHT)

        # ---- Table header ----
        table_fr = tk.Frame(dlg, bg=self.c("panel"))
        table_fr.pack(fill=tk.BOTH, expand=True, padx=10, pady=4)

        header_fr = tk.Frame(table_fr, bg=self.c("hdr_bg"))
        header_fr.pack(fill=tk.X)

        tk.Label(header_fr, text="#", bg=self.c("hdr_bg"), fg=self.c("hdr_fg"),
                 font=("Segoe UI", 9, "bold"), width=4,
                 anchor=tk.CENTER).pack(side=tk.LEFT)
        tk.Label(header_fr, text="U+", bg=self.c("hdr_bg"),
                 fg=self.c("hdr_fg"), font=("Consolas", 9, "bold"), width=7,
                 anchor=tk.W).pack(side=tk.LEFT, padx=(4, 0))

        for si, fp in enumerate(font_paths):
            fname = os.path.basename(fp)
            display_name = fname if len(fname) <= 14 else fname[:12] + ".."
            tk.Label(header_fr, text=display_name, bg=self.c("hdr_bg"),
                     fg=self.c("hdr_fg"), font=("Segoe UI", 9, "bold"), width=10,
                     anchor=tk.W).pack(side=tk.LEFT, padx=2)

        # ---- Scrollable rows ----
        scr_canvas = tk.Canvas(table_fr, bg=self.c("bg"), highlightthickness=0)
        scrollbar = tk.Scrollbar(table_fr, orient=tk.VERTICAL, command=scr_canvas.yview)
        scroll_frame = tk.Frame(scr_canvas, bg=self.c("bg"))

        scroll_frame.bind("<Configure>",
                          lambda e: scr_canvas.configure(scrollregion=scr_canvas.bbox("all")))
        scr_canvas.create_window((0, 0), window=scroll_frame, anchor=tk.NW)
        scr_canvas.configure(yscrollcommand=scrollbar.set)

        scr_canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scrollbar.pack(side=tk.LEFT, fill=tk.Y)

        def _on_mousewheel(event):
            scr_canvas.yview_scroll(int(-1 * (event.delta / 120)), "units")
        dlg.bind_all("<MouseWheel>", _on_mousewheel)

        # ---- Build rows ----
        row_data = []
        dlg._photo_refs = []  # attach to dialog to prevent GC after method returns

        for idx, cp in enumerate(conflict_cps):
            char = chr(cp)
            slot_indices = set(conflicts[cp])

            row_fr = tk.Frame(scroll_frame, bg=self.c("card"))
            row_fr.pack(fill=tk.X, pady=1)

            # Index
            tk.Label(row_fr, text=str(idx + 1), bg=self.c("card"), fg=self.c("text_dim"),
                     font=("Consolas", 9), width=4, anchor=tk.CENTER
                     ).pack(side=tk.LEFT, fill=tk.Y)

            # Codepoint
            tk.Label(row_fr, text=f"U+{cp:04X}", bg=self.c("card"), fg=self.c("text"),
                     font=("Consolas", 9), width=7, anchor=tk.W
                     ).pack(side=tk.LEFT, padx=(4, 0))

            # Font columns
            cell_frames = {}
            for si in range(num_fonts):
                if si in slot_indices:
                    cell_fr = tk.Frame(row_fr, bg=self.c("card"), cursor="hand2",
                                       highlightthickness=0, padx=1, pady=1)
                    cell_fr.pack(side=tk.LEFT, padx=2, fill=tk.Y)

                    font_img = _render_char(char, font_paths[si])
                    dlg._photo_refs.append(font_img)
                    fc = tk.Canvas(cell_fr, width=CELL_W, height=CELL_H,
                                   bg=self.c("card"), highlightthickness=0,
                                   cursor="hand2")
                    fc.create_image(CELL_W // 2, CELL_H // 2, image=font_img)
                    fc.pack()

                    def _select(cp_val=cp, slot_idx=si):
                        resolution[cp_val] = slot_idx
                        _highlight(cp_val)

                    fc.bind("<Button-1>", lambda e, fn=_select: fn())
                    cell_fr.bind("<Button-1>", lambda e, fn=_select: fn())
                    cell_frames[si] = cell_fr
                else:
                    empty_cv = tk.Canvas(row_fr, width=CELL_W, height=CELL_H,
                                         bg=self.c("bg"), highlightthickness=0)
                    empty_cv.pack(side=tk.LEFT, padx=2)

            row_data.append({"cp": cp, "cells": cell_frames})

        def _highlight(cp_val):
            for rw in row_data:
                if rw["cp"] != cp_val:
                    continue
                chosen = resolution.get(cp_val)
                for si, frame in rw["cells"].items():
                    if chosen == si:
                        frame.config(highlightbackground=self.c("sel_border"),
                                     highlightcolor=self.c("sel_border"),
                                     highlightthickness=3)
                    else:
                        frame.config(highlightbackground=self.c("border"),
                                     highlightcolor=self.c("border"),
                                     highlightthickness=0)
                break

        _res_file = {"path": None}  # shared path between save/load

        def _save_resolution():
            font_name = (name_var.get().strip() if name_var else None) or "custom_font"
            initial = _res_file["path"] or f"{font_name}_conflicts.json"
            p = filedialog.asksaveasfilename(
                parent=dlg,
                defaultextension=".json",
                filetypes=[("JSON", "*.json"), ("All", "*.*")],
                initialfile=os.path.basename(initial),
                initialdir=os.path.dirname(initial) if os.path.dirname(initial) else None)
            if not p:
                return
            try:
                save_conflict_resolution(p, resolution, font_paths)
                _res_file["path"] = p
                status_var.set(f"{self.t('conflict_saved')} → {os.path.basename(p)}")
                dlg.after(3000, lambda: status_var.set(""))
            except Exception as e:
                status_var.set(f"ERROR: {e}")
                dlg.after(3000, lambda: status_var.set(""))

        def _load_resolution():
            initial = _res_file["path"]
            p = filedialog.askopenfilename(
                parent=dlg,
                filetypes=[("JSON", "*.json"), ("All", "*.*")],
                initialdir=os.path.dirname(initial) if initial else None,
                initialfile=os.path.basename(initial) if initial else None)
            if not p:
                return
            try:
                loaded = load_conflict_resolution(p)
                font_names = {os.path.basename(fp): i
                              for i, fp in enumerate(font_paths)}
                applied = {}
                for cp, fname in loaded.items():
                    if cp in conflicts and fname in font_names:
                        applied[cp] = font_names[fname]
                if applied:
                    resolution.update(applied)
                    for cp in conflict_cps:
                        if cp not in resolution:
                            resolution[cp] = conflicts[cp][0]
                    for rw in row_data:
                        _highlight(rw["cp"])
                    _res_file["path"] = p
                    status_var.set(f"{self.t('conflict_loaded')} ← {os.path.basename(p)}")
                    dlg.after(3000, lambda: status_var.set(""))
                else:
                    status_var.set("No matching entries found")
                    dlg.after(3000, lambda: status_var.set(""))
            except Exception as e:
                status_var.set(f"ERROR: {e}")
                dlg.after(3000, lambda: status_var.set(""))

        # ---- Bottom bar: apply all to font ----
        bottom_fr = tk.Frame(dlg, bg=self.c("panel"))
        bottom_fr.pack(fill=tk.X, padx=10, pady=(4, 8))

        tk.Label(bottom_fr, text=self.t("conflict_apply_all"), bg=self.c("panel"),
                 fg=self.c("text"), font=("Segoe UI", 8)).pack(side=tk.LEFT)

        for si, fp in enumerate(font_paths):
            fname = os.path.basename(fp)
            btn_text = f"#{si + 1}: {fname[:10]}"

            def _apply_all(slot_idx=si):
                for rw in row_data:
                    resolution[rw["cp"]] = slot_idx
                    _highlight(rw["cp"])

            tk.Button(bottom_fr, text=btn_text, command=_apply_all,
                      bg=self.c("btn_bg"), fg=self.c("btn_fg"), relief=tk.FLAT,
                      padx=6, font=("Segoe UI", 7)).pack(side=tk.LEFT, padx=2)

        # ---- Close handler ----
        def _on_close():
            dlg.unbind_all("<MouseWheel>")
            dlg.destroy()
            if on_done:
                on_done(resolution, [])

        dlg.protocol("WM_DELETE_WINDOW", _on_close)

        # Initial highlights
        for rw in row_data:
            _highlight(rw["cp"])
