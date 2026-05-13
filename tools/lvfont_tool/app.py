"""FontToolApp: Main application class for the LVGL Font Tool."""

import sys
import os
import json
import re
import subprocess
import threading
import ctypes
import unicodedata

import tkinter as tk
from tkinter import font as tkfont, filedialog, messagebox
from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont, ImageTk

from .constants import UNICODE_BLOCKS, get_block, RENDER_BATCH
from .i18n import LANG
from .themes import THEMES
from .generate import GenerateMixin


class FontToolApp(GenerateMixin):

    def __init__(self, root, font_path=None):
        self.root = root
        self.lang = "zh"
        self._theme = "dark"
        self.font_path = None
        self.font_family = None
        self._font_handle = None
        self._tk_font = None
        self.codepoints = []
        self.filtered = []
        self.glyph_names = {}
        self.selected_cp = None
        self._cell_map = {}
        self._cp_to_rect = {}
        self._sel_rects = {}
        self.selected_chars = set()
        self._select_mode = False
        self._render_pending = None
        self._render_gen = None
        self._cell = 28
        self._last_canvas_w = 0
        self._resize_pending = None
        self._drag_start = None
        self._drag_rect_id = None
        self._is_dragging = False
        self._preview_border_mode = "placeholder"
        self._project_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")

        self._build_ui()
        if font_path:
            self.load_font(font_path)

    def t(self, key):
        return LANG[self.lang].get(key, key)

    def c(self, key):
        return THEMES[self._theme][key]

    @property
    def cell(self):
        return self._cell

    @property
    def cols(self):
        w = self.canvas.winfo_width()
        if w < 2:
            w = 800
        return max(1, w // self.cell)

    def _build_ui(self):
        self.root.title(self.t("title"))
        self.root.geometry("1200x750")
        self.root.minsize(900, 500)
        self.root.configure(bg=self.c("bg"))

        # --- Top bar ---
        self.top = tk.Frame(self.root, bg=self.c("panel"), height=38)
        self.top.pack(fill=tk.X)
        self.top.pack_propagate(False)

        self.btn_open = tk.Button(self.top, text=self.t("open"), command=self._open_file,
                                  bg=self.c("btn_primary"), fg="white", relief=tk.FLAT, padx=10,
                                  font=("Segoe UI", 9))
        self.btn_open.pack(side=tk.LEFT, padx=6, pady=5)

        self.lbl_font = tk.Label(self.top, text=self.t("no_font"), bg=self.c("panel"),
                                 fg=self.c("btn_fg"), font=("Segoe UI", 9))
        self.lbl_font.pack(side=tk.LEFT, padx=6)

        self.lbl_stats = tk.Label(self.top, text="", bg=self.c("panel"), fg=self.c("text_muted"),
                                  font=("Segoe UI", 8))
        self.lbl_stats.pack(side=tk.LEFT)

        self.btn_mode = tk.Button(self.top, text=self.t("select_mode"),
                                  command=self._toggle_mode,
                                  bg=self.c("btn_bg"), fg="white", relief=tk.FLAT, padx=8,
                                  font=("Segoe UI", 8))
        self.btn_mode.pack(side=tk.LEFT, padx=(10, 0), pady=5)

        self.btn_sel_all = tk.Button(self.top, text=self.t("select_all"),
                                     command=self._select_all_visible,
                                     bg=self.c("btn_bg"), fg=self.c("btn_fg"), relief=tk.FLAT, padx=6,
                                     font=("Segoe UI", 8), state=tk.DISABLED)
        self.btn_sel_all.pack(side=tk.LEFT, padx=2, pady=5)

        # Preset range buttons
        self._preset_btns = []
        for key, label_key in [("ascii", "p_ascii"), ("latin", "p_latin"),
                               ("symbols", "p_symbols"), ("arrows", "p_arrows"),
                               ("math", "p_math"), ("box", "p_box"),
                               ("gb2312", "p_gb2312"), ("cjk", "p_cjk")]:
            btn = tk.Button(self.top, text=self.t(label_key),
                            command=lambda k=key: self._select_preset(k),
                            bg=self.c("btn_bg"), fg=self.c("btn_fg"), relief=tk.FLAT, padx=4,
                            font=("Segoe UI", 7), state=tk.DISABLED)
            btn.pack(side=tk.LEFT, padx=1, pady=5)
            self._preset_btns.append((key, btn))

        self.btn_generate = tk.Button(self.top, text=self.t("generate"),
                                      command=self._open_generate_dialog,
                                      bg=self.c("btn_green"), fg="white", relief=tk.FLAT, padx=8,
                                      font=("Segoe UI", 8, "bold"))
        self.btn_generate.pack(side=tk.LEFT, padx=(10, 0), pady=5)

        # Right side buttons
        self.btn_lang = tk.Button(self.top, text=self.t("lang_btn"), command=self._toggle_lang,
                                  bg=self.c("btn_bg"), fg="white", relief=tk.FLAT, padx=8,
                                  font=("Segoe UI", 8))
        self.btn_lang.pack(side=tk.RIGHT, padx=(0, 6), pady=5)

        self.btn_theme = tk.Button(self.top, text=self.t("theme_dark"),
                                   command=self._toggle_theme,
                                   bg=self.c("btn_bg"), fg="white", relief=tk.FLAT, padx=8,
                                   font=("Segoe UI", 8))
        self.btn_theme.pack(side=tk.RIGHT, padx=(0, 2), pady=5)

        self.size_fr = tk.Frame(self.top, bg=self.c("panel"))
        self.size_fr.pack(side=tk.RIGHT, padx=(0, 4))
        self.size_var = tk.IntVar(value=16)
        self.lbl_size = tk.Label(self.size_fr, text=self.t("size"), bg=self.c("panel"),
                                 fg=self.c("text_dim"),
                                 font=("Segoe UI", 8))
        self.lbl_size.pack(side=tk.LEFT, padx=(4, 2), pady=10)
        self.size_scale = tk.Scale(self.size_fr, from_=8, to=48, orient=tk.HORIZONTAL,
                                   variable=self.size_var, command=self._on_size_change,
                                   length=100, sliderlength=14, width=12,
                                   bg=self.c("panel"), fg=self.c("btn_fg"), highlightthickness=0,
                                   troughcolor=self.c("btn_bg"), font=("Consolas", 7),
                                   showvalue=True)
        self.size_scale.pack(side=tk.LEFT)

        # Progress bar
        self.progress = tk.Frame(self.root, bg=self.c("bg"), height=3)
        self.progress.pack(fill=tk.X)
        self.progress_bar = tk.Frame(self.progress, bg=self.c("progress"), height=3)
        self.progress_bar.place(x=0, y=0, relheight=1.0, width=0)

        # --- Main ---
        self.main_frame = tk.Frame(self.root, bg=self.c("bg"))
        self.main_frame.pack(fill=tk.BOTH, expand=True, padx=6, pady=(2, 6))

        # Left: canvas
        self.left_frame = tk.Frame(self.main_frame, bg=self.c("bg"))
        self.left_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        self.vscroll = tk.Scrollbar(self.left_frame, orient=tk.VERTICAL)
        self.vscroll.pack(side=tk.RIGHT, fill=tk.Y)
        self.canvas = tk.Canvas(self.left_frame, bg=self.c("bg"), yscrollcommand=self.vscroll.set,
                                highlightthickness=0)
        self.canvas.pack(fill=tk.BOTH, expand=True)
        self.vscroll.config(command=self.canvas.yview)
        self.canvas.bind("<ButtonPress-1>", self._on_press)
        self.canvas.bind("<B1-Motion>", self._on_drag)
        self.canvas.bind("<ButtonRelease-1>", self._on_release)
        self.canvas.bind("<ButtonPress-3>", self._on_right_press)
        self.canvas.bind("<B3-Motion>", self._on_right_drag)
        self.canvas.bind("<ButtonRelease-3>", self._on_right_release)
        self.canvas.bind("<Motion>", self._on_hover)
        self.canvas.bind("<MouseWheel>",
                         lambda e: self.canvas.yview_scroll(-1 * (e.delta // 120), "units"))
        self.canvas.bind("<Button-4>", lambda e: self.canvas.yview_scroll(-3, "units"))
        self.canvas.bind("<Button-5>", lambda e: self.canvas.yview_scroll(3, "units"))
        self.canvas.bind("<Configure>", self._on_canvas_resize)

        # Right panel
        self.right = tk.Frame(self.main_frame, bg=self.c("panel"), width=280)
        self.right.pack(side=tk.RIGHT, fill=tk.Y, padx=(6, 0))
        self.right.pack_propagate(False)

        # Char preview
        self.lbl_char_info = tk.Label(self.right, text=self.t("char_info"), bg=self.c("panel"),
                                      fg=self.c("title_fg"), font=("Segoe UI", 10, "bold"))
        self.lbl_char_info.pack(pady=(8, 2))

        self.preview_canvas = tk.Canvas(self.right, bg=self.c("preview_bg"), width=200, height=120,
                                        highlightthickness=0, cursor="hand2")
        self.preview_canvas.pack(padx=8, pady=4)
        self.preview_canvas.bind("<Button-1>", self._on_preview_click)

        # Border mode toggle
        self.btn_border = tk.Button(self.right, text=self.t("border_placeholder"),
                                    command=self._toggle_border_mode,
                                    bg=self.c("btn_bg"), fg=self.c("btn_fg"),
                                    relief=tk.FLAT, padx=6, font=("Segoe UI", 7))
        self.btn_border.pack(pady=(0, 4))

        self.info_labels = {}
        self._info_key_frames = []
        for key in ["codepoint", "name", "block_col", "glyph", "utf8", "utf16"]:
            row = tk.Frame(self.right, bg=self.c("panel"))
            row.pack(fill=tk.X, padx=8, pady=1)
            self._info_key_frames.append(row)
            k = tk.Label(row, text=self.t(key) + ":", bg=self.c("panel"), fg=self.c("text_muted"),
                         font=("Segoe UI", 8), width=7, anchor=tk.W)
            k.pack(side=tk.LEFT)
            v = tk.Label(row, text="-", bg=self.c("panel"), fg=self.c("text"),
                         font=("Consolas", 9), anchor=tk.W)
            v.pack(side=tk.LEFT, fill=tk.X, expand=True)
            self.info_labels[key] = (k, v)

        # Separator
        self._sep1 = tk.Frame(self.right, bg=self.c("border"), height=1)
        self._sep1.pack(fill=tk.X, padx=8, pady=(8, 4))

        # Selection panel
        self.sel_hdr = tk.Frame(self.right, bg=self.c("panel"))
        self.sel_hdr.pack(fill=tk.X, padx=8)
        self.lbl_selection = tk.Label(self.sel_hdr, text=f"{self.t('selection')} (0)",
                                      bg=self.c("panel"), fg=self.c("text_dim"), font=("Segoe UI", 9, "bold"))
        self.lbl_selection.pack(side=tk.LEFT)
        self.btn_clear_sel = tk.Button(self.sel_hdr, text=self.t("clear_sel"),
                                       command=self._clear_selection,
                                       bg=self.c("btn_bg"), fg=self.c("btn_fg"), relief=tk.FLAT, padx=6,
                                       font=("Segoe UI", 7))
        self.btn_clear_sel.pack(side=tk.RIGHT)

        self.sel_text_fr = tk.Frame(self.right, bg=self.c("bg"))
        self.sel_text_fr.pack(fill=tk.X, padx=8, pady=(2, 4))
        self.sel_text = tk.Text(self.sel_text_fr, bg=self.c("bg"), fg=self.c("text"),
                                font=("Consolas", 10), height=3, wrap=tk.WORD,
                                relief=tk.FLAT, highlightthickness=0,
                                insertbackground=self.c("text"))
        self.sel_text.pack(fill=tk.X, padx=2, pady=2)
        self.sel_text.config(state=tk.DISABLED)

        # Export buttons
        self.export_fr = tk.Frame(self.right, bg=self.c("panel"))
        self.export_fr.pack(fill=tk.X, padx=8, pady=(0, 4))
        self.btn_export_copy = tk.Button(self.export_fr, text=self.t("export_copy"),
                                         command=lambda: self._export_selection(),
                                         bg=self.c("btn_primary"), fg="white", relief=tk.FLAT, padx=6,
                                         font=("Segoe UI", 8))
        self.btn_export_copy.pack(side=tk.LEFT, padx=(0, 3))
        self.btn_export_file = tk.Button(self.export_fr, text=self.t("export_file"),
                                         command=self._export_symbols_file,
                                         bg=self.c("btn_green"), fg="white", relief=tk.FLAT, padx=8,
                                         font=("Segoe UI", 8, "bold"))
        self.btn_export_file.pack(side=tk.LEFT, padx=(0, 3))
        self.btn_import = tk.Button(self.export_fr, text=self.t("import_file"),
                                    command=self._import_symbols_file,
                                    bg=self.c("btn_bg"), fg=self.c("btn_fg"), relief=tk.FLAT, padx=8,
                                    font=("Segoe UI", 8))
        self.btn_import.pack(side=tk.LEFT)

        # Separator
        self._sep2 = tk.Frame(self.right, bg=self.c("border"), height=1)
        self._sep2.pack(fill=tk.X, padx=8, pady=(4, 4))

        # Search
        self.lbl_search = tk.Label(self.right, text=self.t("search"), bg=self.c("panel"),
                                   fg=self.c("text_dim"),
                                   font=("Segoe UI", 9))
        self.lbl_search.pack(anchor=tk.W, padx=8)
        self.search_var = tk.StringVar()
        self.search_var.trace_add("write", lambda *_: self._apply_filter())
        self.search_entry = tk.Entry(self.right, textvariable=self.search_var, bg=self.c("input_bg"),
                                     fg=self.c("input_fg"),
                                     insertbackground=self.c("input_fg"), font=("Consolas", 9),
                                     relief=tk.FLAT)
        self.search_entry.pack(fill=tk.X, padx=8, pady=2)
        self.lbl_search_hint = tk.Label(self.right, text=self.t("search_hint"), bg=self.c("panel"),
                                        fg=self.c("ctrl_fg"), font=("Segoe UI", 7))
        self.lbl_search_hint.pack(anchor=tk.W, padx=8)

        # Separator
        self._sep3 = tk.Frame(self.right, bg=self.c("border"), height=1)
        self._sep3.pack(fill=tk.X, padx=8, pady=(8, 4))

        # Block list
        self.bl_hdr = tk.Frame(self.right, bg=self.c("panel"))
        self.bl_hdr.pack(fill=tk.X, padx=8)
        self.lbl_blocks = tk.Label(self.bl_hdr, text=self.t("blocks_in_font"), bg=self.c("panel"),
                                   fg=self.c("text_dim"), font=("Segoe UI", 9, "bold"))
        self.lbl_blocks.pack(side=tk.LEFT)
        self.btn_show_all = tk.Button(self.bl_hdr, text=self.t("show_all"), command=self._show_all,
                                      bg=self.c("btn_bg"), fg=self.c("btn_fg"), relief=tk.FLAT, padx=6,
                                      font=("Segoe UI", 7))
        self.btn_show_all.pack(side=tk.RIGHT)

        self.bl_fr = tk.Frame(self.right, bg=self.c("panel"))
        self.bl_fr.pack(fill=tk.BOTH, expand=True, padx=8, pady=(2, 8))
        bl_scroll = tk.Scrollbar(self.bl_fr, orient=tk.VERTICAL)
        bl_scroll.pack(side=tk.RIGHT, fill=tk.Y)
        self.block_list = tk.Listbox(self.bl_fr, bg=self.c("list_bg"), fg=self.c("list_fg"),
                                     selectbackground=self.c("sel_bg"),
                                     font=("Consolas", 8), height=16,
                                     highlightthickness=0, relief=tk.FLAT,
                                     activestyle="none",
                                     yscrollcommand=bl_scroll.set)
        self.block_list.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        bl_scroll.config(command=self.block_list.yview)
        self.block_list.bind("<<ListboxSelect>>", self._on_block_select)

        # Status bar
        self.status_var = tk.StringVar(value="")
        self.status_bar = tk.Label(self.root, textvariable=self.status_var, bg=self.c("panel"),
                                   fg=self.c("text_muted"),
                                   anchor=tk.W, font=("Segoe UI", 7), padx=6)
        self.status_bar.pack(fill=tk.X, side=tk.BOTTOM)

    # ---- Progress / Resize ----

    def _set_progress(self, pct):
        self.root.update_idletasks()
        w = self.progress.winfo_width()
        self.progress_bar.place(x=0, y=0, relheight=1.0, width=max(0, int(w * pct / 100)))

    def _on_canvas_resize(self, event):
        new_w = event.width
        if new_w == self._last_canvas_w:
            return
        self._last_canvas_w = new_w
        if self._resize_pending:
            self.root.after_cancel(self._resize_pending)
        self._resize_pending = self.root.after(200, self._do_resize_render)

    def _do_resize_render(self):
        self._resize_pending = None
        if self.font_path and self.filtered:
            self._render_async()

    # ---- Lang / Mode ----

    def _toggle_mode(self):
        self._select_mode = not self._select_mode
        state = tk.NORMAL if self._select_mode else tk.DISABLED
        self.btn_mode.config(
            bg=self.c("btn_primary") if self._select_mode else self.c("btn_bg"),
            text=self.t("info_mode") if self._select_mode else self.t("select_mode"))
        self.btn_sel_all.config(state=state)
        for _, btn in self._preset_btns:
            btn.config(state=state)

    def _toggle_theme(self):
        self._theme = "light" if self._theme == "dark" else "dark"
        self._apply_theme()

    def _apply_theme(self):
        c = self.c
        self.root.configure(bg=c("bg"))
        self.root.title(self.t("title"))

        # Top bar
        self.top.config(bg=c("panel"))
        self.btn_open.config(bg=c("btn_primary"), fg="white")
        self.lbl_font.config(bg=c("panel"), fg=c("btn_fg"))
        self.lbl_stats.config(bg=c("panel"), fg=c("text_muted"))
        self.btn_mode.config(
            bg=c("btn_primary") if self._select_mode else c("btn_bg"), fg=c("btn_accent_fg"))
        self.btn_sel_all.config(bg=c("btn_bg"), fg=c("btn_fg"))
        for _, btn in self._preset_btns:
            btn.config(bg=c("btn_bg"), fg=c("btn_fg"))
        self.btn_generate.config(bg=c("btn_green"), fg="white")
        self.btn_lang.config(bg=c("btn_bg"), fg=c("btn_accent_fg"))
        self.btn_theme.config(text=self.t(f"theme_{self._theme}"), bg=c("btn_bg"), fg=c("btn_accent_fg"))
        self.size_fr.config(bg=c("panel"))
        self.lbl_size.config(bg=c("panel"), fg=c("text_dim"))
        self.size_scale.config(bg=c("panel"), fg=c("btn_fg"), troughcolor=c("btn_bg"))

        # Progress
        self.progress.config(bg=c("bg"))
        self.progress_bar.config(bg=c("progress"))

        # Main frame
        self.main_frame.config(bg=c("bg"))
        self.left_frame.config(bg=c("bg"))

        # Canvas
        self.canvas.config(bg=c("bg"))

        # Right panel
        self.right.config(bg=c("panel"))
        self.lbl_char_info.config(bg=c("panel"), fg=c("title_fg"))
        self.preview_canvas.config(bg=c("preview_bg"))
        self.btn_border.config(bg=c("btn_bg"), fg=c("btn_fg"))
        for row in self._info_key_frames:
            row.config(bg=c("panel"))
        for key, (lk, lv) in self.info_labels.items():
            lk.config(bg=c("panel"), fg=c("text_muted"))
            lv.config(bg=c("panel"), fg=c("text"))

        # Separators
        self._sep1.config(bg=c("border"))
        self._sep2.config(bg=c("border"))
        self._sep3.config(bg=c("border"))

        # Selection panel
        self.sel_hdr.config(bg=c("panel"))
        self.lbl_selection.config(bg=c("panel"), fg=c("text_dim"))
        self.btn_clear_sel.config(bg=c("btn_bg"), fg=c("btn_fg"))
        self.sel_text_fr.config(bg=c("bg"))
        self.sel_text.config(bg=c("bg"), fg=c("text"), insertbackground=c("text"))

        # Export buttons
        self.export_fr.config(bg=c("panel"))
        self.btn_export_copy.config(bg=c("btn_primary"), fg="white")
        self.btn_export_file.config(bg=c("btn_green"), fg="white")
        self.btn_import.config(bg=c("btn_bg"), fg=c("btn_fg"))

        # Search
        self.lbl_search.config(bg=c("panel"), fg=c("text_dim"))
        self.search_entry.config(bg=c("input_bg"), fg=c("input_fg"), insertbackground=c("input_fg"))
        self.lbl_search_hint.config(bg=c("panel"), fg=c("ctrl_fg"))

        # Block list
        self.bl_hdr.config(bg=c("panel"))
        self.lbl_blocks.config(bg=c("panel"), fg=c("text_dim"))
        self.btn_show_all.config(bg=c("btn_bg"), fg=c("btn_fg"))
        self.bl_fr.config(bg=c("panel"))
        self.block_list.config(bg=c("list_bg"), fg=c("list_fg"), selectbackground=c("sel_bg"))

        # Status bar
        self.status_bar.config(bg=c("panel"), fg=c("text_muted"))

        # Re-render canvas content
        if self.filtered:
            self._render_async()
        # Re-render preview if character is selected
        if self.selected_cp is not None and self.font_path:
            self._select_char(self.selected_cp)

    def _toggle_lang(self):
        self.lang = "en" if self.lang == "zh" else "zh"
        self._refresh_labels()
        if self.filtered:
            self._render_async()

    def _refresh_labels(self):
        self.root.title(self.t("title"))
        for attr, key in [("btn_open", "open"), ("btn_lang", "lang_btn"),
                          ("btn_mode", "select_mode" if not self._select_mode else "info_mode"),
                          ("btn_sel_all", "select_all"), ("btn_generate", "generate"),
                          ("btn_export_file", "export_file"), ("btn_clear_sel", "clear_sel"),
                          ("lbl_search", "search"), ("lbl_search_hint", "search_hint"),
                          ("lbl_blocks", "blocks_in_font"), ("btn_show_all", "show_all"),
                          ("lbl_char_info", "char_info"), ("lbl_size", "size"),
                          ("btn_theme", f"theme_{self._theme}")]:
            getattr(self, attr).config(text=self.t(key))
        self.lbl_selection.config(text=f"{self.t('selection')} ({len(self.selected_chars)})")
        for key, (lk, _) in self.info_labels.items():
            lk.config(text=self.t(key) + ":")
        self.btn_export_copy.config(text=self.t("export_copy"))
        for key, btn in self._preset_btns:
            btn.config(text=self.t(f"p_{key}"))
        self.btn_import.config(text=self.t("import_file"))
        self.btn_border.config(text=self.t(f"border_{self._preview_border_mode}"))
        if not self.font_path:
            self.lbl_font.config(text=self.t("no_font"))
        if hasattr(self, '_blocks_sorted'):
            self._populate_block_list()

    # ---- File ops ----

    def _open_file(self):
        path = filedialog.askopenfilename(
            filetypes=[("Font files", "*.ttf *.otf *.ttc *.woff *.woff2"), ("All", "*.*")])
        if path:
            self.load_font(path)

    def _show_all(self):
        self.search_var.set("")
        self.block_list.selection_clear(0, tk.END)
        self.filtered = list(self.codepoints)
        self._render_async()

    def _load_native_font(self, path):
        self._unload_native_font()
        font = TTFont(path)
        family = None
        for record in font['name'].names:
            if record.nameID == 1:
                try:
                    family = record.toUnicode()
                    break
                except Exception:
                    pass
        font.close()
        if not family:
            return None
        if sys.platform == "win32":
            try:
                ctypes.windll.gdi32.AddFontResourceExW(path, 0x10, 0)
                self._font_handle = path
            except Exception:
                pass
        return family

    def _unload_native_font(self):
        if self._font_handle and sys.platform == "win32":
            try:
                ctypes.windll.gdi32.RemoveFontResourceExW(self._font_handle, 0x10, 0)
            except Exception:
                pass
            self._font_handle = None

    def load_font(self, path):
        try:
            self._set_progress(5)
            font = TTFont(path)
            self.font_path = path
            self.font_name = os.path.basename(path)
            self.cmap = font.getBestCmap()
            self.codepoints = sorted(self.cmap.keys())
            self.glyph_names = dict(self.cmap)
            font.close()

            self._set_progress(20)
            self.font_family = self._load_native_font(path)

            self._update_tk_font()
            self.lbl_font.config(text=self.font_name)
            self.lbl_stats.config(text=f"{len(self.codepoints):,} {self.t('glyphs')}")

            self._populate_blocks()
            self.search_var.set("")
            self._set_progress(40)
            self._apply_filter()
            self.status_var.set(
                f"{self.t('loaded')}: {self.font_name} — {len(self.codepoints):,} {self.t('glyphs')}")
        except Exception as e:
            self._set_progress(0)
            messagebox.showerror("Error", str(e))

    def _update_tk_font(self):
        size = self.size_var.get()
        self._cell = max(20, size + 12)
        if self.font_family:
            try:
                self._tk_font = tkfont.Font(family=self.font_family, size=size)
            except Exception:
                self._tk_font = None

    # ---- Blocks ----

    def _populate_blocks(self):
        self._blocks_with_chars = {}
        for cp in self.codepoints:
            en, cn = get_block(cp)
            if en not in self._blocks_with_chars:
                self._blocks_with_chars[en] = {"en": en, "cn": cn, "count": 0, "start": cp}
            self._blocks_with_chars[en]["count"] += 1
        self._blocks_sorted = sorted(self._blocks_with_chars.values(),
                                     key=lambda b: b["start"])
        self._populate_block_list()

    def _populate_block_list(self):
        self.block_list.delete(0, tk.END)
        self._block_list_keys = []
        for b in self._blocks_sorted:
            name = b["cn"] if self.lang == "zh" else b["en"]
            self.block_list.insert(tk.END, f"{name} [{b['count']}]")
            self._block_list_keys.append(b["en"])

    # ---- Filter ----

    def _apply_filter(self):
        search = self.search_var.get().strip()
        result = []
        search_cp = search_char = None
        upper_search = ""
        if search:
            try:
                search_cp = int(search, 16)
            except ValueError:
                pass
            if len(search) == 1:
                search_char = ord(search)
            upper_search = search.upper()

        for cp in self.codepoints:
            if search_cp is not None or search_char is not None or search:
                matched = False
                if search_cp is not None and cp == search_cp:
                    matched = True
                if search_char is not None and cp == search_char:
                    matched = True
                if not matched and search:
                    if upper_search in f"{cp:04X}":
                        matched = True
                    try:
                        if upper_search in unicodedata.name(chr(cp), "").upper():
                            matched = True
                    except Exception:
                        pass
                    if search in chr(cp):
                        matched = True
                if not matched:
                    continue
            result.append(cp)

        self.filtered = result
        self._render_async()

    # ---- Render ----

    def _render_async(self):
        if self._render_pending:
            self.root.after_cancel(self._render_pending)
            self._render_pending = None
        self._render_gen = None
        self.canvas.delete("all")
        self._cell_map.clear()
        self._cp_to_rect.clear()
        self._sel_rects.clear()
        if not self.filtered:
            self._set_progress(0)
            return
        self._update_tk_font()
        self._render_gen = self._render_gen_blocks()
        self._render_step()

    def _render_gen_blocks(self):
        cell = self.cell
        cols = self.cols
        canvas_w = self.canvas.winfo_width()
        if canvas_w < 2:
            canvas_w = 800
        hdr_w = canvas_w
        total = len(self.filtered)

        headers = []
        char_items = []
        y, i, prev_block = 0, 0, None

        while i < total:
            block_en, _ = get_block(self.filtered[i])
            if block_en != prev_block:
                binfo = self._blocks_with_chars.get(block_en, {})
                name = binfo.get("cn" if self.lang == "zh" else "en", block_en) \
                    if isinstance(binfo, dict) and "cn" in binfo else block_en
                headers.append((y, name, hdr_w))
                y += 20
                prev_block = block_en
            start = i
            while i < total and get_block(self.filtered[i])[0] == block_en:
                i += 1
            block_cps = self.filtered[start:i]
            for j, cp in enumerate(block_cps):
                col, row = j % cols, j // cols
                char_items.append((col * cell, y + row * cell, cp))
            rows = (len(block_cps) + cols - 1) // cols
            y += rows * cell

        for hy, name, hw in headers:
            self.canvas.create_rectangle(0, hy, hw, hy + 20, fill=self.c("hdr_bg"), outline="")
            self.canvas.create_text(6, hy + 10, text=name, fill=self.c("hdr_fg"),
                                    anchor=tk.W, font=("Segoe UI", 8, "bold"))
        self.canvas.config(scrollregion=(0, 0, hdr_w, y))

        total_items = len(char_items)
        for bi in range(0, total_items, RENDER_BATCH):
            batch = char_items[bi:bi + RENDER_BATCH]
            rendered = []
            for x, yr, cp in batch:
                bbox = (x, yr, x + cell, yr + cell)
                self._cell_map[bbox] = cp
                self._cp_to_rect[cp] = bbox
                rendered.append((x, yr, cp))
            pct = int((bi + len(batch)) * 60 / total_items) + 40
            yield rendered, pct

    def _render_step(self):
        if self._render_gen is None:
            return
        try:
            items, pct = next(self._render_gen)
        except StopIteration:
            self._set_progress(0)
            self._render_gen = None
            self._restore_selection_highlights()
            self.status_var.set(f"{len(self.filtered):,} {self.t('glyphs')}")
            return
        cell = self.cell
        font = self._tk_font
        half = cell // 2
        for x, yr, cp in items:
            if cp < 0x20 or cp == 0x7F:
                self.canvas.create_text(x + half, yr + half, text=f"{cp:02X}",
                                        fill=self.c("ctrl_fg"), font=("Consolas", 7))
            elif cp != 0x20:
                try:
                    self.canvas.create_text(x + half, yr + half, text=chr(cp),
                                            fill=self.c("cell_fg"), font=font)
                except Exception:
                    self.canvas.create_text(x + half, yr + half, text="?",
                                            fill=self.c("err_fg"), font=("Consolas", 7))
        self._set_progress(pct)
        done, total = len(self._cell_map), len(self.filtered)
        self.status_var.set(f"{self.t('loading')}... {min(done, total)}/{total}")
        self._render_pending = self.root.after(1, self._render_step)

    # ---- Cell lookup ----

    def _find_cell(self, x, y):
        for (x1, y1, x2, y2), cp in self._cell_map.items():
            if x1 <= x < x2 and y1 <= y < y2:
                return cp
        return None

    def _find_cells_in_rect(self, rx1, ry1, rx2, ry2):
        half = self.cell // 2
        result = []
        for cp, (x1, y1, x2, y2) in self._cp_to_rect.items():
            cx, cy = x1 + half, y1 + half
            if rx1 <= cx <= rx2 and ry1 <= cy <= ry2:
                result.append(cp)
        return result

    # ---- Mouse: click + drag ----

    def _on_press(self, event):
        cx = self.canvas.canvasx(event.x)
        cy = self.canvas.canvasy(event.y)
        self._drag_start = (cx, cy)
        self._is_dragging = False

    def _on_drag(self, event):
        if self._drag_start is None or not self._select_mode:
            return
        cx = self.canvas.canvasx(event.x)
        cy = self.canvas.canvasy(event.y)
        sx, sy = self._drag_start
        if not self._is_dragging:
            if abs(cx - sx) < 5 and abs(cy - sy) < 5:
                return
            self._is_dragging = True
        if self._drag_rect_id:
            self.canvas.delete(self._drag_rect_id)
        self._drag_rect_id = self.canvas.create_rectangle(
            min(sx, cx), min(sy, cy), max(sx, cx), max(sy, cy),
            outline=self.c("sel_border"), width=1, dash=(3, 3))

    def _on_release(self, event):
        if self._drag_start is None:
            return
        sx, sy = self._drag_start
        if self._is_dragging:
            if not self._select_mode:
                if self._drag_rect_id:
                    self.canvas.delete(self._drag_rect_id)
                    self._drag_rect_id = None
                self._drag_start = None
                self._is_dragging = False
                return
            if self._drag_rect_id:
                self.canvas.delete(self._drag_rect_id)
                self._drag_rect_id = None
            cx = self.canvas.canvasx(event.x)
            cy = self.canvas.canvasy(event.y)
            cells = self._find_cells_in_rect(min(sx, cx), min(sy, cy),
                                              max(sx, cx), max(sy, cy))
            for cp in cells:
                if cp not in self.selected_chars:
                    self.selected_chars.add(cp)
                    self._add_highlight(cp)
            self._update_sel_display()
        else:
            cp = self._find_cell(sx, sy)
            if cp is not None:
                if self._select_mode:
                    if cp not in self.selected_chars:
                        self.selected_chars.add(cp)
                        self._add_highlight(cp)
                        self._update_sel_display()
                self._select_char(cp)
        self._drag_start = None
        self._is_dragging = False

    def _on_right_press(self, event):
        if not self._select_mode:
            return
        cx = self.canvas.canvasx(event.x)
        cy = self.canvas.canvasy(event.y)
        self._drag_start = (cx, cy)
        self._is_dragging = False

    def _on_right_drag(self, event):
        if self._drag_start is None or not self._select_mode:
            return
        cx = self.canvas.canvasx(event.x)
        cy = self.canvas.canvasy(event.y)
        sx, sy = self._drag_start
        if not self._is_dragging:
            if abs(cx - sx) < 5 and abs(cy - sy) < 5:
                return
            self._is_dragging = True
        if self._drag_rect_id:
            self.canvas.delete(self._drag_rect_id)
        self._drag_rect_id = self.canvas.create_rectangle(
            min(sx, cx), min(sy, cy), max(sx, cx), max(sy, cy),
            outline="#e05050", width=1, dash=(3, 3))

    def _on_right_release(self, event):
        if not self._select_mode or self._drag_start is None:
            return
        sx, sy = self._drag_start
        if self._is_dragging:
            if self._drag_rect_id:
                self.canvas.delete(self._drag_rect_id)
                self._drag_rect_id = None
            cx = self.canvas.canvasx(event.x)
            cy = self.canvas.canvasy(event.y)
            cells = self._find_cells_in_rect(min(sx, cx), min(sy, cy),
                                              max(sx, cx), max(sy, cy))
            for cp in cells:
                if cp in self.selected_chars:
                    self.selected_chars.discard(cp)
                    self._remove_highlight(cp)
            self._update_sel_display()
        else:
            cp = self._find_cell(sx, sy)
            if cp is not None and cp in self.selected_chars:
                self.selected_chars.discard(cp)
                self._remove_highlight(cp)
                self._update_sel_display()
        self._drag_start = None
        self._is_dragging = False

    def _on_hover(self, event):
        cp = self._find_cell(self.canvas.canvasx(event.x), self.canvas.canvasy(event.y))
        if cp is not None:
            ch = chr(cp)
            en, cn = get_block(cp)
            name = unicodedata.name(ch, "")
            block = cn if self.lang == "zh" else en
            sel_mark = " [SEL]" if cp in self.selected_chars else ""
            self.status_var.set(f"U+{cp:04X}  {name}  [{block}]{sel_mark}")

    # ---- Selection highlights (bug-fixed) ----

    def _add_highlight(self, cp):
        bbox = self._cp_to_rect.get(cp)
        if not bbox:
            return
        x1, y1, x2, y2 = bbox
        rid = self.canvas.create_rectangle(x1 + 1, y1 + 1, x2 - 1, y2 - 1,
                                           outline=self.c("sel_border"), width=2)
        self._sel_rects[cp] = rid

    def _remove_highlight(self, cp):
        rid = self._sel_rects.pop(cp, None)
        if rid is not None:
            try:
                self.canvas.delete(rid)
            except tk.TclError:
                pass

    def _restore_selection_highlights(self):
        # Delete ALL old rects first (canvas.delete("all") already did, but
        # user-added rects during async render may be orphaned)
        for rid in self._sel_rects.values():
            try:
                self.canvas.delete(rid)
            except tk.TclError:
                pass
        self._sel_rects.clear()
        for cp in self.selected_chars:
            if cp in self._cp_to_rect:
                self._add_highlight(cp)

    def _toggle_select(self, cp):
        if cp in self.selected_chars:
            self.selected_chars.discard(cp)
            self._remove_highlight(cp)
        else:
            self.selected_chars.add(cp)
            self._add_highlight(cp)
        self._update_sel_display()

    def _update_sel_display(self):
        count = len(self.selected_chars)
        self.lbl_selection.config(text=f"{self.t('selection')} ({count})")
        self.sel_text.config(state=tk.NORMAL)
        self.sel_text.delete("1.0", tk.END)
        self.sel_text.insert("1.0", "".join(chr(cp) for cp in sorted(self.selected_chars)))
        self.sel_text.config(state=tk.DISABLED)

    def _clear_selection(self):
        for cp in list(self._sel_rects.keys()):
            self._remove_highlight(cp)
        self.selected_chars.clear()
        self._sel_rects.clear()
        self._update_sel_display()

    def _select_all_visible(self):
        for cp in self.filtered:
            if cp not in self.selected_chars:
                self.selected_chars.add(cp)
                self._add_highlight(cp)
        self._update_sel_display()

    # ---- Preset range selection ----

    _PRESETS = {
        "ascii":    lambda cp: 0x0020 <= cp <= 0x007E,
        "latin":    lambda cp: 0x0080 <= cp <= 0x024F,
        "symbols":  lambda cp: (0x2000 <= cp <= 0x206F or   # General Punctuation
                                0x2100 <= cp <= 0x214F or   # Letterlike Symbols
                                0x2150 <= cp <= 0x218F or   # Number Forms
                                0x2460 <= cp <= 0x24FF or   # Enclosed Alphanumerics
                                0x2500 <= cp <= 0x257F or   # Box Drawing
                                0x2580 <= cp <= 0x259F or   # Block Elements
                                0x25A0 <= cp <= 0x26FF or   # Geometric + Misc Symbols
                                0x2700 <= cp <= 0x27BF or   # Dingbats
                                0x3000 <= cp <= 0x303F or   # CJK Symbols/Punctuation
                                0xFE30 <= cp <= 0xFE6F or   # CJK Compat Forms + Small
                                0xFF00 <= cp <= 0xFFEF),    # Halfwidth/Fullwidth
        "arrows":   lambda cp: (0x2190 <= cp <= 0x21FF or
                                0x27F0 <= cp <= 0x27FF or
                                0x2900 <= cp <= 0x29FF or
                                0x2B00 <= cp <= 0x2BFF),
        "math":     lambda cp: (0x2200 <= cp <= 0x22FF or
                                0x27C0 <= cp <= 0x27EF or
                                0x2A00 <= cp <= 0x2AFF),
        "box":      lambda cp: 0x2500 <= cp <= 0x259F,
        "gb2312":   None,  # special handling
        "cjk":      lambda cp: 0x4E00 <= cp <= 0x9FFF,
    }

    def _select_preset(self, key):
        if not self._select_mode:
            return
        if key == "gb2312":
            self._select_gb2312()
            return
        pred = self._PRESETS.get(key)
        if pred is None:
            return
        added = 0
        for cp in self.codepoints:
            if pred(cp) and cp not in self.selected_chars:
                self.selected_chars.add(cp)
                if cp in self._cp_to_rect:
                    self._add_highlight(cp)
                added += 1
        self._update_sel_display()

    def _select_gb2312(self):
        # GB2312 zones 01-09 (symbols) + 16-87 (CJK)
        gb_cps = set()
        for qu in range(1, 88):
            if 10 <= qu <= 15:
                continue
            for wei in range(1, 95):
                b1 = qu + 0xA0
                b2 = wei + 0xA0
                try:
                    ch = bytes([b1, b2]).decode('gb2312')
                    gb_cps.add(ord(ch))
                except (UnicodeDecodeError, ValueError):
                    pass
        added = 0
        for cp in self.codepoints:
            if cp in gb_cps and cp not in self.selected_chars:
                self.selected_chars.add(cp)
                if cp in self._cp_to_rect:
                    self._add_highlight(cp)
                added += 1
        self._update_sel_display()

    # ---- Char detail ----

    def _select_char(self, cp):
        self.selected_cp = cp
        ch = chr(cp)
        en, cn = get_block(cp)
        self.preview_canvas.delete("all")
        try:
            large_font = ImageFont.truetype(self.font_path, 56)
            bbox = large_font.getbbox(ch)
            gw = bbox[2] - bbox[0]
            gh = bbox[3] - bbox[1]
            # Canvas drawable area
            cw, ch_canvas = 200, 120
            margin = 16
            # Scale to fit, keeping aspect ratio
            scale = min((cw - 2 * margin) / max(gw, 1), (ch_canvas - 2 * margin) / max(gh, 1))
            dw = int(gw * scale)
            dh = int(gh * scale)
            ox = (cw - dw) // 2
            oy = (ch_canvas - dh) // 2
            # Render glyph centered
            fg = self.c("preview_fg")[:3]
            img = Image.new("RGBA", (cw, ch_canvas), self.c("preview_img_bg"))
            draw = ImageDraw.Draw(img)
            draw.text((cw // 2, ch_canvas // 2), ch, fill=fg,
                      font=large_font, anchor="mm")
            self._detail_tk_img = ImageTk.PhotoImage(img)
            self.preview_canvas.create_image(cw // 2, ch_canvas // 2,
                                              image=self._detail_tk_img, anchor=tk.CENTER)
            # Border mode
            if self._preview_border_mode == "glyph":
                # Character actual bounding box
                self.preview_canvas.create_rectangle(
                    ox, oy, ox + dw, oy + dh,
                    outline=self.c("sel_border"), width=1)
            else:
                # Render placeholder (font size square)
                size_px = 56
                sw = int(size_px * scale)
                sh = int(size_px * scale)
                sx = (cw - sw) // 2
                sy = (ch_canvas - sh) // 2
                self.preview_canvas.create_rectangle(
                    sx, sy, sx + sw, sy + sh,
                    outline=self.c("sel_border"), width=2)
        except Exception:
            pass
        name = unicodedata.name(ch, "-")
        block = cn if self.lang == "zh" else en
        utf8 = " ".join(f"{b:02X}" for b in ch.encode("utf-8"))
        utf16 = " ".join(f"{b:02X}" for b in ch.encode("utf-16-be" if cp <= 0xFFFF else "utf-32-be"))
        self.info_labels["codepoint"][1].config(text=f"U+{cp:04X} ({cp})")
        self.info_labels["name"][1].config(text=name)
        self.info_labels["block_col"][1].config(text=block)
        self.info_labels["glyph"][1].config(text=self.glyph_names.get(cp, "-"))
        self.info_labels["utf8"][1].config(text=utf8)
        self.info_labels["utf16"][1].config(text=utf16)

    # ---- Block / Size callbacks ----

    def _on_block_select(self, event):
        sel = self.block_list.curselection()
        if not sel or not hasattr(self, '_block_list_keys'):
            return
        block_en = self._block_list_keys[sel[0]]
        self.search_var.set("")
        self.filtered = [cp for cp in self.codepoints if get_block(cp)[0] == block_en]
        self._render_async()

    def _on_size_change(self, _=None):
        if self.font_path and self.filtered:
            self._render_async()
            if self.selected_cp is not None:
                self._select_char(self.selected_cp)

    # ---- Export ----

    def _export_selection(self):
        if not self.selected_chars:
            return
        chars = sorted(self.selected_chars)
        text = "".join(chr(cp) for cp in chars)
        self.root.clipboard_clear()
        self.root.clipboard_append(text)
        self._show_toast(f"{self.t('exported')} ({len(chars)})")

    def _export_symbols_file(self):
        if not self.selected_chars:
            return
        path = filedialog.asksaveasfilename(
            defaultextension=".txt",
            filetypes=[("Text files", "*.txt"), ("All", "*.*")],
            initialfile="font_symbols.txt")
        if not path:
            return
        chars = sorted(self.selected_chars)
        with open(path, "w", encoding="utf-8") as f:
            f.write("".join(chr(cp) for cp in chars))
        self._show_toast(f"{self.t('saved')} {len(chars)} → {os.path.basename(path)}")

    def _import_symbols_file(self):
        path = filedialog.askopenfilename(
            filetypes=[("Text files", "*.txt"), ("All", "*.*")])
        if not path:
            return
        with open(path, "r", encoding="utf-8") as f:
            text = f.read()
        font_cps = set(self.codepoints) if self.codepoints else None
        added = 0
        for ch in text:
            cp = ord(ch)
            if font_cps is not None and cp not in font_cps:
                continue
            if cp not in self.selected_chars:
                self.selected_chars.add(cp)
                if cp in self._cp_to_rect:
                    self._add_highlight(cp)
                added += 1
        self._update_sel_display()
        self._show_toast(f"{self.t('import')} +{added}")

    def _toggle_border_mode(self):
        if self._preview_border_mode == "placeholder":
            self._preview_border_mode = "glyph"
        else:
            self._preview_border_mode = "placeholder"
        self.btn_border.config(text=self.t(f"border_{self._preview_border_mode}"))
        if self.selected_cp is not None and self.font_path:
            self._select_char(self.selected_cp)

    def _on_preview_click(self, event):
        if self.selected_cp is not None:
            ch = chr(self.selected_cp)
            self.root.clipboard_clear()
            self.root.clipboard_append(ch)
            self._show_toast(f"{self.t('copied')} {ch}")

    def _show_toast(self, text):
        toast = tk.Label(self.preview_canvas, text=text, bg="#3a6ea5", fg="white",
                         font=("Segoe UI", 9), padx=10, pady=4)
        toast.place(relx=0.5, rely=0.5, anchor=tk.CENTER)
        self.preview_canvas.after(1200, lambda: toast.destroy())

    # ---- Cleanup ----

    def on_close(self):
        self._unload_native_font()
        self.root.destroy()


def main():
    root = tk.Tk()
    app = FontToolApp(root)
    root.protocol("WM_DELETE_WINDOW", app.on_close)
    root.mainloop()
