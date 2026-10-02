#!/usr/bin/env python3
"""Generate the GB2312 stroke dictionary and embedded Unifont glyphs."""

import argparse
from pathlib import Path


UI_TEXT = (
    "中文界面编辑历史设置预览确认发送模式笔画候选符号输入清空打印网络连接"
    "无消息等待就绪大写小写数字选择返回失败加载主页语言半角全角横竖撇点折"
    "聊天终端开有回复用户消息提示否是模型推理上下文自动打印字号行距走纸词典？"
               "保存失败打印机未就绪发送中离线默认关匹配选择网络无网络输入密码"
               "密码过短连接成功连接中连接失败扫描中扫描失败网络密码重试中重试失败无可重试消息"
    "，。？！：；（）、「」『』《》【】“”＋－＝／％＆＊＃＠～·…￥↵"
)


def parse_bdf(path):
    lines = Path(path).read_text(encoding="ascii", errors="ignore").splitlines()
    glyphs = {}
    i = 0
    while i < len(lines):
        if not lines[i].startswith("STARTCHAR U+"):
            i += 1
            continue
        encoding = None
        rows = None
        i += 1
        while i < len(lines) and not lines[i].startswith("ENDCHAR"):
            line = lines[i]
            if line.startswith("ENCODING "):
                encoding = int(line.split()[1])
            elif line.strip() == "BITMAP":
                i += 1
                bitmap = []
                while i < len(lines) and not lines[i].startswith("ENDCHAR"):
                    bitmap.append(int(lines[i].strip() or "0", 16))
                    i += 1
                rows = bitmap
                continue
            i += 1
        if encoding is not None and rows is not None:
            glyphs[encoding] = (rows + [0] * 16)[:16]
        i += 1
    return glyphs


def parse_frequency(path):
    ordered = []
    seen = set()
    for line_number, line in enumerate(
        Path(path).read_text(encoding="utf-8").splitlines(), 1
    ):
        if not line:
            continue
        fields = line.split("\t")
        if len(fields) != 2 or len(fields[0]) != 1:
            raise SystemExit(f"invalid frequency row at line {line_number}")
        char, raw_frequency = fields
        try:
            frequency = int(raw_frequency)
            char.encode("gb2312")
        except (ValueError, UnicodeEncodeError) as error:
            raise SystemExit(f"invalid GB2312 frequency row at line {line_number}: {error}")
        if char in seen:
            raise SystemExit(f"duplicate frequency character at line {line_number}: {char}")
        seen.add(char)
        ordered.append((char, frequency, line_number))
    ordered.sort(key=lambda row: (-row[1], row[2]))
    if len(ordered) < 6763:
        raise SystemExit(f"frequency file has only {len(ordered)} characters; expected GB2312 coverage")
    return ordered


def parse_dictionary(path):
    entries = {}
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#") or line.startswith("---") or "\t" not in line:
            continue
        word, raw_codes = line.split("\t", 1)
        if len(word) != 1:
            continue
        codes = entries.setdefault(word, [])
        code = raw_codes.split()[0] if raw_codes.split() else ""
        if code and code not in codes:
            codes.append(code)
    return entries


def c_string(text):
    return "".join(f"\\x{byte:02X}" for byte in text.encode("utf-8"))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--stroke", required=True)
    parser.add_argument("--frequency", required=True)
    parser.add_argument("--bdf", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--glyph-output", required=True)
    args = parser.parse_args()

    frequency = parse_frequency(args.frequency)
    strokes = parse_dictionary(args.stroke)
    glyphs = parse_bdf(args.bdf)
    missing_codes = [char for char, _, _ in frequency if char not in strokes]
    if missing_codes:
        raise SystemExit(f"stroke dictionary missing {len(missing_codes)} GB2312 characters: {''.join(missing_codes[:80])}")

    ui_text = set(UI_TEXT)
    used_codepoints = {ord(char) for char, _, _ in frequency} | {
        ord(char) for char in ui_text
    } | set(range(0x20, 0x7F))
    missing_glyphs = sorted(chr(codepoint) for codepoint in used_codepoints if codepoint not in glyphs)
    if missing_glyphs:
        raise SystemExit("Unifont is missing required glyphs: " + "".join(missing_glyphs[:80]))

    flattened_codes = []
    characters = []
    for char, _, _ in frequency:
        codes = strokes[char]
        offset = len(flattened_codes)
        flattened_codes.extend(codes)
        if offset > 0xFFFF or len(codes) > 0xFF:
            raise SystemExit("stroke dictionary exceeds compact index limits")
        characters.append((char, offset, len(codes)))

    dictionary_path = Path(args.output)
    dictionary_path.parent.mkdir(parents=True, exist_ok=True)
    with dictionary_path.open("w", encoding="ascii") as out:
        out.write("// Generated from Rime stroke.dict.yaml and gb2312_by_freq.txt.\n")
        out.write("// Rime data: BSD-3-Clause. Frequency order is supplied by the project.\n")
        out.write("#pragma once\n#include <cstddef>\n#include <cstdint>\n\n")
        out.write("namespace thermal_terminal::stroke_data {\n")
        out.write("struct Character { const char *utf8; std::uint16_t stroke_offset; std::uint8_t stroke_count; };\n")
        out.write("inline constexpr Character kCharacters[] = {\n")
        for char, offset, count in characters:
            out.write(f'  {{"{c_string(char)}", {offset}, {count}}},\n')
        out.write("};\ninline constexpr std::size_t kCharacterCount = sizeof(kCharacters)/sizeof(kCharacters[0]);\n")
        out.write("static_assert(kCharacterCount >= 6763, \"The stroke dictionary must cover GB2312\");\n")
        out.write("inline constexpr const char *kStrokeCodes[] = {\n")
        for code in flattened_codes:
            out.write(f'  "{code}",\n')
        out.write("};\ninline constexpr std::size_t kStrokeCodeCount = sizeof(kStrokeCodes)/sizeof(kStrokeCodes[0]);\n")
        out.write("} // namespace thermal_terminal::stroke_data\n")

    glyph_path = Path(args.glyph_output)
    glyph_path.parent.mkdir(parents=True, exist_ok=True)
    with glyph_path.open("w", encoding="ascii") as out:
        out.write("// Generated from GNU Unifont 16x16 BDF.\n")
        out.write("// Unifont is distributed under GPLv2-or-later and SIL OFL terms with the font embedding exception.\n")
        out.write("#pragma once\n#include <array>\n#include <cstddef>\n#include <cstdint>\n\n")
        out.write("namespace thermal_terminal::unifont_data {\n")
        out.write("struct Glyph { std::uint16_t codepoint; std::array<std::uint16_t,16> rows; };\n")
        out.write("inline constexpr Glyph kGlyphs[] = {\n")
        for codepoint in sorted(used_codepoints):
            rows = ", ".join(f"0x{value:04X}" for value in glyphs[codepoint])
            out.write(f"  {{{codepoint}, {{{rows}}}}},\n")
        out.write("};\ninline constexpr std::size_t kGlyphCount = sizeof(kGlyphs)/sizeof(kGlyphs[0]);\n")
        out.write("static_assert(kGlyphCount >= 6763, \"Unifont glyphs must cover GB2312\");\n")
        out.write("} // namespace thermal_terminal::unifont_data\n")

    print(
        f"generated {len(characters)} GB2312 characters, "
        f"{len(flattened_codes)} stroke codes, {len(used_codepoints)} glyphs"
    )


if __name__ == "__main__":
    main()
