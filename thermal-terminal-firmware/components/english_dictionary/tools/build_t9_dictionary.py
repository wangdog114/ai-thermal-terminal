#!/usr/bin/env python3
"""Pack en_wordlist.combined into a compact T9 lookup file."""

import argparse
import struct
from pathlib import Path

MAGIC = b"T9D1"
GROUPS = 10000
HEADER_SIZE = 16
INDEX_SIZE = GROUPS * 8


def t9_code(word):
    keys = {
        **dict.fromkeys("abc", "2"), **dict.fromkeys("def", "3"),
        **dict.fromkeys("ghi", "4"), **dict.fromkeys("jkl", "5"),
        **dict.fromkeys("mno", "6"), **dict.fromkeys("pqrs", "7"),
        **dict.fromkeys("tuv", "8"), **dict.fromkeys("wxyz", "9"),
    }
    result = "".join(keys.get(char.lower(), "") for char in word)
    return result if result and all(char.isascii() for char in word) else ""


def parse(path):
    records = []
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        if not line.startswith(" word="):
            continue
        fields = line.strip().split(",")
        word = fields[0][5:]
        if not word or len(word.encode("ascii", errors="ignore")) != len(word):
            continue
        if len(word) > 31 or any(ord(char) < 32 for char in word):
            continue
        frequency = 0
        for field in fields[1:]:
            if field.startswith("f="):
                try:
                    frequency = max(0, min(65535, int(field[2:])))
                except ValueError:
                    pass
                break
        code = t9_code(word)
        if code and len(code) <= 31:
            records.append((code, word, frequency))
    records.sort(key=lambda item: (0 if len(item[0]) < 4 else int(item[0][:4]),
                                   item[0], -item[2], item[1]))
    return records


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("input")
    parser.add_argument("output")
    args = parser.parse_args()
    records = parse(args.input)
    offsets = [(0, 0)] * GROUPS
    blob = bytearray()
    first_group = [None] * GROUPS
    last_group = [None] * GROUPS
    for code, word, frequency in records:
        start = HEADER_SIZE + INDEX_SIZE + len(blob)
        group = 0 if len(code) < 4 else int(code[:4])
        first_group[group] = start if first_group[group] is None else first_group[group]
        blob.extend(struct.pack("<BBH", len(code), len(word), frequency))
        packed = bytearray((len(code) + 1) // 2)
        for index, digit in enumerate(code):
            value = ord(digit) - ord("0")
            if index % 2 == 0:
                packed[index // 2] = value << 4
            else:
                packed[index // 2] |= value
        blob.extend(packed)
        blob.extend(word.encode("ascii"))
        last_group[group] = HEADER_SIZE + INDEX_SIZE + len(blob)
    index = bytearray()
    for group in range(GROUPS):
        start = first_group[group] or 0
        end = last_group[group] or start
        index.extend(struct.pack("<II", start, end))
    header = struct.pack("<4sHHII", MAGIC, 1, 0, len(records), HEADER_SIZE + INDEX_SIZE)
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(header + index + blob)
    print(f"packed {len(records)} words into {output} ({output.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
