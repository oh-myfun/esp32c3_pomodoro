#!/usr/bin/env node
// Generate LVGL font files from Maple Mono CN + Noto Emoji
// Bypasses shell command line length limits by calling lv_font_conv API directly

'use strict';

const fs = require('fs');
const path = require('path');
const convert = require('../node_modules/lv_font_conv/lib/convert');

const FONT_REGULAR = 'fonts/MapleMono-CN-Regular.ttf';
const FONT_EMOJI = 'fonts/NotoEmoji-Regular.ttf';
const OUT_DIR = 'main/ui';

async function genFont(size, name) {
    console.log(`Generating ${name} (${size}px)...`);

    const args = {
        size: size,
        bpp: 4,
        format: 'lvgl',
        font: [
            {
                source_path: FONT_REGULAR,
                source_bin: fs.readFileSync(FONT_REGULAR),
                ranges: [{ symbols: fs.readFileSync('fonts/gb2312_symbols.txt', 'utf8') }]
            },
            {
                source_path: FONT_EMOJI,
                source_bin: fs.readFileSync(FONT_EMOJI),
                ranges: [{ symbols: fs.readFileSync('fonts/emoji_symbols.txt', 'utf8') }]
            }
        ],
        output: `${OUT_DIR}/${name}.c`,
        lv_font_name: name,
        no_compress: true
    };

    const files = await convert(args);

    for (const [filename, content] of Object.entries(files)) {
        // Fix include path for ESP-IDF
        const fixed = content.replace(
            /#include "lvgl\/lvgl\.h"/g,
            '#include "lvgl.h"'
        );
        fs.writeFileSync(filename, fixed);
        const sizeBytes = Buffer.byteLength(fixed);
        console.log(`  -> ${path.basename(filename)} (${(sizeBytes / 1024).toFixed(0)} KB)`);
    }
}

async function main() {
    process.chdir(path.join(__dirname, '..'));

    console.log('=== Maple Mono CN + Noto Emoji Font Generation ===');
    console.log('');

    await genFont(14, 'custom_font_14');
    await genFont(16, 'custom_font_16');

    console.log('');
    console.log('=== Done ===');
}

main().catch(err => {
    console.error('Error:', err.message || err);
    process.exit(1);
});
