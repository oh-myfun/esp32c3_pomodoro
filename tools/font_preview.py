"""
Font Character Preview Tool

Usage:
    python tools/font_preview.py [font_file]
    python tools/font_preview.py fonts/MapleMono-CN-Regular.ttf
"""

import sys
import os
import tkinter as tk
from tkinter import font as tkfont, filedialog, messagebox
from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont, ImageTk
import unicodedata
import ctypes

UNICODE_BLOCKS = [
    (0x0000, 0x001F, "Control Characters", "控制字符"),
    (0x0020, 0x007F, "Basic Latin", "基本拉丁"),
    (0x0080, 0x00FF, "Latin-1 Supplement", "拉丁-1 补充"),
    (0x0100, 0x024F, "Latin Extended-A/B", "拉丁扩展A/B"),
    (0x0250, 0x02FF, "IPA / Modifiers", "音标/修饰符"),
    (0x0300, 0x036F, "Combining Marks", "组合变音符"),
    (0x0370, 0x03FF, "Greek and Coptic", "希腊和科普特"),
    (0x0400, 0x052F, "Cyrillic", "西里尔"),
    (0x0530, 0x058F, "Armenian", "亚美尼亚"),
    (0x0590, 0x05FF, "Hebrew", "希伯来"),
    (0x0600, 0x06FF, "Arabic", "阿拉伯"),
    (0x0700, 0x08FF, "Syriac / Thaana / NKo", "叙利亚/塔纳"),
    (0x0900, 0x0DFF, "Devanagari / Bengali / ...", "天城文/孟加拉等"),
    (0x0E00, 0x0FFF, "Thai / Lao / Tibetan", "泰文/老挝/藏文"),
    (0x1000, 0x1FFF, "Myanmar / Georgian / Hangul", "缅甸/格鲁吉亚/韩文"),
    (0x2000, 0x206F, "General Punctuation", "通用标点"),
    (0x2070, 0x209F, "Super/Subscripts", "上下标"),
    (0x20A0, 0x20CF, "Currency Symbols", "货币符号"),
    (0x20D0, 0x20FF, "Combining Symbol Marks", "组合符号标记"),
    (0x2100, 0x214F, "Letterlike Symbols", "字母式符号"),
    (0x2150, 0x218F, "Number Forms", "数字形式"),
    (0x2190, 0x21FF, "Arrows", "箭头"),
    (0x2200, 0x22FF, "Math Operators", "数学运算符"),
    (0x2300, 0x23FF, "Misc Technical", "杂项技术"),
    (0x2400, 0x245F, "Control Pictures / OCR", "控制图/OCR"),
    (0x2460, 0x24FF, "Enclosed Alphanumerics", "包围字母数字"),
    (0x2500, 0x257F, "Box Drawing", "制表符"),
    (0x2580, 0x259F, "Block Elements", "方块元素"),
    (0x25A0, 0x25FF, "Geometric Shapes", "几何形状"),
    (0x2600, 0x26FF, "Misc Symbols", "杂项符号"),
    (0x2700, 0x27BF, "Dingbats", "装饰符号"),
    (0x27C0, 0x27EF, "Math Symbols-A", "数学符号A"),
    (0x27F0, 0x27FF, "Supplemental Arrows-A", "补充箭头A"),
    (0x2800, 0x28FF, "Braille Patterns", "盲文"),
    (0x2900, 0x29FF, "Supplemental Arrows-B", "补充箭头B"),
    (0x2A00, 0x2BFF, "Math Symbols-B / Misc Arrows", "数学符号B/杂项箭头"),
    (0x2C00, 0x2DFF, "Glagolitic / Latin Ext-C / Coptic", "格拉哥里/拉丁扩展C"),
    (0x2E00, 0x2EFF, "Supplemental Punctuation", "补充标点"),
    (0x2F00, 0x2FDF, "Kangxi Radicals", "康熙部首"),
    (0x2FF0, 0x2FFF, "Ideographic Description", "表意描述字符"),
    (0x3000, 0x303F, "CJK Symbols and Punctuation", "CJK 符号标点"),
    (0x3040, 0x309F, "Hiragana", "平假名"),
    (0x30A0, 0x30FF, "Katakana", "片假名"),
    (0x3100, 0x31FF, "Bopomofo / Hangul Jamo", "注音/韩文字母"),
    (0x3200, 0x33FF, "Enclosed CJK / CJK Compat", "包围CJK/兼容"),
    (0x3400, 0x4DBF, "CJK Extension A", "CJK 扩展A"),
    (0x4DC0, 0x4DFF, "Yijing Hexagrams", "易经六十四卦"),
    (0x4E00, 0x9FFF, "CJK Unified Ideographs", "CJK 统一表意"),
    (0xA000, 0xABFF, "Yi / Lisu / Vai", "彝文/傈僳/瓦伊"),
    (0xAC00, 0xD7AF, "Hangul Syllables", "韩文音节"),
    (0xD7B0, 0xDFFF, "Hangul Jamo Ext-B", "韩文扩展B"),
    (0xE000, 0xF8FF, "Private Use Area", "私人使用区"),
    (0xF900, 0xFAFF, "CJK Compatibility Ideographs", "CJK 兼容表意"),
    (0xFB00, 0xFE0F, "Presentation Forms / Variation", "表现形式/变体"),
    (0xFE10, 0xFE1F, "Vertical Forms", "竖排形式"),
    (0xFE20, 0xFE2F, "Combining Half Marks", "组合半标记"),
    (0xFE30, 0xFE4F, "CJK Compatibility Forms", "CJK 兼容形式"),
    (0xFE50, 0xFE6F, "Small Form Variants", "小型变体"),
    (0xFE70, 0xFEFF, "Arabic Presentation-B", "阿拉伯形式B"),
    (0xFF00, 0xFFEF, "Halfwidth and Fullwidth Forms", "半角全角形式"),
    (0xFFF0, 0xFFFF, "Specials", "特殊字符"),
    (0x10000, 0x107FF, "Linear B / Greek Ext", "线形B/希腊扩展"),
    (0x10800, 0x1D3FF, "Ancient Scripts", "古文字"),
    (0x1D400, 0x1D7FF, "Math Alphanumeric Symbols", "数学字母符号"),
    (0x1D800, 0x1EFFF, "Sutton SignWriting / Glagolitic", "手语/格拉哥里"),
    (0x1F000, 0x1F02F, "Mahjong Tiles", "麻将牌"),
    (0x1F030, 0x1F09F, "Domino Tiles", "多米诺牌"),
    (0x1F0A0, 0x1F0FF, "Playing Cards", "扑克牌"),
    (0x1F100, 0x1F1FF, "Enclosed Alphanumeric Supp", "包围字母数字补充"),
    (0x1F200, 0x1F2FF, "Enclosed Ideographic Supp", "包围表意补充"),
    (0x1F300, 0x1F5FF, "Misc Symbols and Pictographs", "杂项符号象形"),
    (0x1F600, 0x1F64F, "Emoticons", "表情"),
    (0x1F650, 0x1F67F, "Ornamental Dingbats", "装饰符号"),
    (0x1F680, 0x1F6FF, "Transport and Map Symbols", "交通地图符号"),
    (0x1F700, 0x1F77F, "Alchemical Symbols", "炼金术符号"),
    (0x1F780, 0x1F7FF, "Geometric Shapes Extended", "几何形状扩展"),
    (0x1F800, 0x1F8FF, "Supplemental Arrows-C", "补充箭头C"),
    (0x1F900, 0x1F9FF, "Supplemental Symbols-A", "补充符号A"),
    (0x1FA00, 0x1FAFF, "Symbols Extended-A", "符号扩展A"),
    (0x1FB00, 0x1FFFF, "Symbols for Legacy Computing", "传统计算符号"),
    (0x20000, 0x2A6DF, "CJK Extension B", "CJK 扩展B"),
    (0x2A700, 0x2CEAF, "CJK Extension C/D", "CJK 扩展C/D"),
    (0x2CEB0, 0x2EBEF, "CJK Extension E/F", "CJK 扩展E/F"),
    (0x2F800, 0x2FA1F, "CJK Compat Ideographs Supp", "CJK兼容表意补充"),
    (0x30000, 0x3FFFF, "CJK Extension G/H", "CJK 扩展G/H"),
    (0xE0000, 0xE007F, "Tags", "标签"),
    (0xE0100, 0xE01EF, "Variation Selectors Supp", "变体选择符补充"),
    (0xF0000, 0xFFFFD, "Supplementary PUA-A", "补充私人使用区A"),
    (0x100000, 0x10FFFD, "Supplementary PUA-B", "补充私人使用区B"),
]


def get_block(codepoint):
    for start, end, en, cn in UNICODE_BLOCKS:
        if start <= codepoint <= end:
            return (en, cn)
    lo = codepoint & ~0xFF
    hi = lo + 0xFF
    return (f"U+{lo:04X}–U+{hi:04X}", f"U+{lo:04X}–U+{hi:04X}")


LANG = {
    "en": {
        "title": "Font Preview",
        "open": "Open", "show_all": "Show All",
        "size": "Size:", "search": "Search:",
        "search_hint": "hex / char / name",
        "char_info": "Character Info (click to copy)",
        "codepoint": "Codepoint", "name": "Name", "block_col": "Block",
        "glyph": "Glyph", "utf8": "UTF-8", "utf16": "UTF-16",
        "glyphs": "glyphs", "no_font": "No font loaded",
        "loaded": "Loaded", "blocks_in_font": "Blocks",
        "lang_btn": "中文", "loading": "Rendering",
        "copied": "Copied!",
    },
    "zh": {
        "title": "字体预览",
        "open": "打开", "show_all": "显示全部",
        "size": "字号:", "search": "搜索:",
        "search_hint": "十六进制 / 字符 / 名称",
        "char_info": "字符信息（点击复制）",
        "codepoint": "码点", "name": "名称", "block_col": "区块",
        "glyph": "字形", "utf8": "UTF-8", "utf16": "UTF-16",
        "glyphs": "个字符", "no_font": "未加载字体",
        "loaded": "已加载", "blocks_in_font": "编码区块",
        "lang_btn": "EN", "loading": "渲染中",
        "copied": "已复制!",
    },
}

RENDER_BATCH = 200


class FontPreviewApp:
    BG = "#1e1e1e"
    CELL_FG = "#d4d4d4"
    HDR_BG = "#333333"
    HDR_FG = "#ffffff"
    SEL_BG = "#3a6ea5"

    def __init__(self, root, font_path=None):
        self.root = root
        self.lang = "zh"
        self.font_path = None
        self.font_family = None
        self._font_handle = None
        self._tk_font = None
        self.codepoints = []
        self.filtered = []
        self.glyph_names = {}
        self.selected_cp = None
        self._cell_map = {}
        self._render_pending = None
        self._render_gen = None
        self._cell = 28
        self._last_canvas_w = 0
        self._resize_pending = None

        self._build_ui()
        if font_path:
            self.load_font(font_path)

    def t(self, key):
        return LANG[self.lang].get(key, key)

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
        self.root.geometry("1100x750")
        self.root.minsize(800, 500)
        self.root.configure(bg=self.BG)

        # --- Top bar ---
        top = tk.Frame(self.root, bg="#252526", height=38)
        top.pack(fill=tk.X)
        top.pack_propagate(False)

        self.btn_open = tk.Button(top, text=self.t("open"), command=self._open_file,
                                  bg="#3a6ea5", fg="white", relief=tk.FLAT, padx=10,
                                  font=("Segoe UI", 9))
        self.btn_open.pack(side=tk.LEFT, padx=6, pady=5)

        self.lbl_font = tk.Label(top, text=self.t("no_font"), bg="#252526",
                                 fg="#ccc", font=("Segoe UI", 9))
        self.lbl_font.pack(side=tk.LEFT, padx=6)

        self.lbl_stats = tk.Label(top, text="", bg="#252526", fg="#888",
                                  font=("Segoe UI", 8))
        self.lbl_stats.pack(side=tk.LEFT)

        # Right side (reverse pack order)
        self.btn_lang = tk.Button(top, text=self.t("lang_btn"), command=self._toggle_lang,
                                  bg="#444", fg="white", relief=tk.FLAT, padx=8,
                                  font=("Segoe UI", 8))
        self.btn_lang.pack(side=tk.RIGHT, padx=(0, 6), pady=5)

        # Font size: label + slider in a sub-frame
        size_fr = tk.Frame(top, bg="#252526")
        size_fr.pack(side=tk.RIGHT, padx=(0, 4))
        self.size_var = tk.IntVar(value=16)
        self.lbl_size = tk.Label(size_fr, text=self.t("size"), bg="#252526", fg="#aaa",
                                 font=("Segoe UI", 8))
        self.lbl_size.pack(side=tk.LEFT, padx=(4, 2), pady=10)
        self.size_scale = tk.Scale(size_fr, from_=8, to=48, orient=tk.HORIZONTAL,
                                   variable=self.size_var, command=self._on_size_change,
                                   length=100, sliderlength=14, width=12,
                                   bg="#252526", fg="#ccc", highlightthickness=0,
                                   troughcolor="#444", font=("Consolas", 7),
                                   showvalue=True)
        self.size_scale.pack(side=tk.LEFT)

        # Progress bar (thin line under top)
        self.progress = tk.Frame(self.root, bg=self.BG, height=3)
        self.progress.pack(fill=tk.X)
        self.progress_bar = tk.Frame(self.progress, bg="#3a6ea5", height=3)
        self.progress_bar.place(x=0, y=0, relheight=1.0, width=0)

        # --- Main ---
        main = tk.Frame(self.root, bg=self.BG)
        main.pack(fill=tk.BOTH, expand=True, padx=6, pady=(2, 6))

        # Left: canvas
        left = tk.Frame(main, bg=self.BG)
        left.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        self.vscroll = tk.Scrollbar(left, orient=tk.VERTICAL)
        self.vscroll.pack(side=tk.RIGHT, fill=tk.Y)
        self.canvas = tk.Canvas(left, bg=self.BG, yscrollcommand=self.vscroll.set,
                                highlightthickness=0)
        self.canvas.pack(fill=tk.BOTH, expand=True)
        self.vscroll.config(command=self.canvas.yview)
        self.canvas.bind("<Button-1>", self._on_click)
        self.canvas.bind("<Motion>", self._on_hover)
        self.canvas.bind("<MouseWheel>",
                         lambda e: self.canvas.yview_scroll(-1 * (e.delta // 120), "units"))
        self.canvas.bind("<Button-4>", lambda e: self.canvas.yview_scroll(-3, "units"))
        self.canvas.bind("<Button-5>", lambda e: self.canvas.yview_scroll(3, "units"))
        self.canvas.bind("<Configure>", self._on_canvas_resize)

        # Right panel
        right = tk.Frame(main, bg="#252526", width=240)
        right.pack(side=tk.RIGHT, fill=tk.Y, padx=(6, 0))
        right.pack_propagate(False)

        # Char preview
        self.lbl_char_info = tk.Label(right, text=self.t("char_info"), bg="#252526",
                                      fg="#fff", font=("Segoe UI", 10, "bold"))
        self.lbl_char_info.pack(pady=(8, 2))

        self.preview_canvas = tk.Canvas(right, bg="#1a1a1a", width=200, height=90,
                                        highlightthickness=0, cursor="hand2")
        self.preview_canvas.pack(padx=8, pady=4)
        self.preview_canvas.bind("<Button-1>", self._on_preview_click)

        self.info_labels = {}
        for key in ["codepoint", "name", "block_col", "glyph", "utf8", "utf16"]:
            row = tk.Frame(right, bg="#252526")
            row.pack(fill=tk.X, padx=8, pady=1)
            k = tk.Label(row, text=self.t(key) + ":", bg="#252526", fg="#888",
                         font=("Segoe UI", 8), width=7, anchor=tk.W)
            k.pack(side=tk.LEFT)
            v = tk.Label(row, text="-", bg="#252526", fg="#e0e0e0",
                         font=("Consolas", 9), anchor=tk.W)
            v.pack(side=tk.LEFT, fill=tk.X, expand=True)
            self.info_labels[key] = (k, v)

        # Separator
        tk.Frame(right, bg="#444", height=1).pack(fill=tk.X, padx=8, pady=(8, 4))

        # Search
        self.lbl_search = tk.Label(right, text=self.t("search"), bg="#252526", fg="#aaa",
                                   font=("Segoe UI", 9))
        self.lbl_search.pack(anchor=tk.W, padx=8)
        self.search_var = tk.StringVar()
        self.search_var.trace_add("write", lambda *_: self._apply_filter())
        tk.Entry(right, textvariable=self.search_var, bg="#3c3c3c", fg="#e0e0e0",
                 insertbackground="#e0e0e0", font=("Consolas", 9),
                 relief=tk.FLAT).pack(fill=tk.X, padx=8, pady=2)
        self.lbl_search_hint = tk.Label(right, text=self.t("search_hint"), bg="#252526",
                                        fg="#666", font=("Segoe UI", 7))
        self.lbl_search_hint.pack(anchor=tk.W, padx=8)

        # Separator
        tk.Frame(right, bg="#444", height=1).pack(fill=tk.X, padx=8, pady=(8, 4))

        # Block list with "Show All" button and scrollbar
        hdr = tk.Frame(right, bg="#252526")
        hdr.pack(fill=tk.X, padx=8)
        self.lbl_blocks = tk.Label(hdr, text=self.t("blocks_in_font"), bg="#252526",
                                   fg="#aaa", font=("Segoe UI", 9, "bold"))
        self.lbl_blocks.pack(side=tk.LEFT)
        self.btn_show_all = tk.Button(hdr, text=self.t("show_all"), command=self._show_all,
                                      bg="#444", fg="#ccc", relief=tk.FLAT, padx=6,
                                      font=("Segoe UI", 7))
        self.btn_show_all.pack(side=tk.RIGHT)

        bl_fr = tk.Frame(right, bg="#252526")
        bl_fr.pack(fill=tk.BOTH, expand=True, padx=8, pady=(2, 8))
        bl_scroll = tk.Scrollbar(bl_fr, orient=tk.VERTICAL)
        bl_scroll.pack(side=tk.RIGHT, fill=tk.Y)
        self.block_list = tk.Listbox(bl_fr, bg="#1e1e1e", fg="#cccccc",
                                     selectbackground=self.SEL_BG,
                                     font=("Consolas", 8), height=16,
                                     highlightthickness=0, relief=tk.FLAT,
                                     activestyle="none",
                                     yscrollcommand=bl_scroll.set)
        self.block_list.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        bl_scroll.config(command=self.block_list.yview)
        self.block_list.bind("<<ListboxSelect>>", self._on_block_select)

        # Status bar
        self.status_var = tk.StringVar(value="")
        tk.Label(self.root, textvariable=self.status_var, bg="#252526", fg="#888",
                 anchor=tk.W, font=("Segoe UI", 7), padx=6).pack(fill=tk.X, side=tk.BOTTOM)

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

    def _toggle_lang(self):
        self.lang = "en" if self.lang == "zh" else "zh"
        self._refresh_labels()
        if self.filtered:
            self._render_async()

    def _refresh_labels(self):
        self.root.title(self.t("title"))
        self.btn_open.config(text=self.t("open"))
        self.btn_lang.config(text=self.t("lang_btn"))
        self.lbl_size.config(text=self.t("size"))
        self.lbl_search.config(text=self.t("search"))
        self.lbl_search_hint.config(text=self.t("search_hint"))
        self.lbl_blocks.config(text=self.t("blocks_in_font"))
        self.btn_show_all.config(text=self.t("show_all"))
        self.lbl_char_info.config(text=self.t("char_info"))
        for key, (lk, _) in self.info_labels.items():
            lk.config(text=self.t(key) + ":")
        if not self.font_path:
            self.lbl_font.config(text=self.t("no_font"))
        if hasattr(self, '_blocks_sorted'):
            self._populate_block_list()

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

    def _render_async(self):
        if self._render_pending:
            self.root.after_cancel(self._render_pending)
            self._render_pending = None
        self._render_gen = None

        self.canvas.delete("all")
        self._cell_map.clear()
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

        # Layout pass: compute all positions (pure math, no canvas calls)
        headers = []
        char_items = []
        y = 0
        i = 0
        prev_block = None

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
                col = j % cols
                row = j // cols
                char_items.append((col * cell, y + row * cell, cp))

            rows = (len(block_cps) + cols - 1) // cols
            y += rows * cell

        # Render all headers at once (few items, instant)
        for hy, name, hw in headers:
            self.canvas.create_rectangle(0, hy, hw, hy + 20,
                                         fill=self.HDR_BG, outline="")
            self.canvas.create_text(6, hy + 10, text=name, fill=self.HDR_FG,
                                    anchor=tk.W, font=("Segoe UI", 8, "bold"))

        # Set final scrollregion upfront
        self.canvas.config(scrollregion=(0, 0, hdr_w, y))

        # Yield char items in fixed-size batches
        total_items = len(char_items)
        for bi in range(0, total_items, RENDER_BATCH):
            batch = char_items[bi:bi + RENDER_BATCH]
            rendered = []
            for x, yr, cp in batch:
                self._cell_map[(x, yr, x + cell, yr + cell)] = cp
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
            self.status_var.set(f"{len(self.filtered):,} {self.t('glyphs')}")
            return

        cell = self.cell
        font = self._tk_font
        half = cell // 2
        for x, yr, cp in items:
            if cp < 0x20 or cp == 0x7F:
                self.canvas.create_text(x + half, yr + half, text=f"{cp:02X}",
                                        fill="#666", font=("Consolas", 7))
            elif cp != 0x20:
                try:
                    self.canvas.create_text(x + half, yr + half, text=chr(cp),
                                            fill=self.CELL_FG, font=font)
                except Exception:
                    self.canvas.create_text(x + half, yr + half, text="?",
                                            fill="#f66", font=("Consolas", 7))

        self._set_progress(pct)
        done = len(self._cell_map)
        total = len(self.filtered)
        self.status_var.set(f"{self.t('loading')}... {min(done, total)}/{total}")
        self._render_pending = self.root.after(1, self._render_step)

    def _find_cell(self, x, y):
        cell = self.cell
        for (x1, y1, x2, y2), cp in self._cell_map.items():
            if x1 <= x < x2 and y1 <= y < y2:
                return cp
        return None

    def _on_click(self, event):
        cp = self._find_cell(self.canvas.canvasx(event.x), self.canvas.canvasy(event.y))
        if cp is not None:
            self._select_char(cp)

    def _on_hover(self, event):
        cp = self._find_cell(self.canvas.canvasx(event.x), self.canvas.canvasy(event.y))
        if cp is not None:
            ch = chr(cp)
            en, cn = get_block(cp)
            name = unicodedata.name(ch, "")
            block = cn if self.lang == "zh" else en
            self.status_var.set(f"U+{cp:04X}  {name}  [{block}]")

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

    def _select_char(self, cp):
        self.selected_cp = cp
        ch = chr(cp)
        en, cn = get_block(cp)

        self.preview_canvas.delete("all")
        try:
            large_font = ImageFont.truetype(self.font_path, 56)
            img = Image.new("RGBA", (200, 90), (26, 26, 26, 255))
            draw = ImageDraw.Draw(img)
            draw.text((100, 45), ch, fill=(255, 255, 255), font=large_font, anchor="mm")
            self._detail_tk_img = ImageTk.PhotoImage(img)
            self.preview_canvas.create_image(100, 45, image=self._detail_tk_img)
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

    def _on_preview_click(self, event):
        if self.selected_cp is not None:
            ch = chr(self.selected_cp)
            self.root.clipboard_clear()
            self.root.clipboard_append(ch)
            self._show_toast(f"{self.t('copied')} {ch}")

    def _show_toast(self, text):
        toast = tk.Label(self.root, text=text, bg="#3a6ea5", fg="white",
                         font=("Segoe UI", 9), padx=10, pady=4)
        toast.place(relx=0.5, rely=0.5, anchor=tk.CENTER)
        self.root.after(1200, lambda: toast.destroy())

    def on_close(self):
        self._unload_native_font()
        self.root.destroy()


def main():
    root = tk.Tk()
    app = FontPreviewApp(root)
    root.protocol("WM_DELETE_WINDOW", app.on_close)
    root.mainloop()


if __name__ == "__main__":
    main()
