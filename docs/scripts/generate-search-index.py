#!/usr/bin/env python3
"""Generate search index from markdown source files.

Outputs either JSON (for debugging) or JS (for inline browser use).
The JS version uses short keys to minimize file size.
"""

import json
import re
import os
import sys


def extract_entries(src_files):
    entries = []

    for src in src_files:
        name = os.path.splitext(os.path.basename(src))[0]

        with open(src, encoding="utf-8") as f:
            md = f.read()

        # Extract page title from YAML front matter
        title = ""
        m = re.search(r"^title:\s*[\"'](.+?)[\"']", md, re.MULTILINE)
        if m:
            title = m.group(1)

        # Split on h2/h3 headings
        sections = re.split(r"^(#{2,3})\s+(.+)$", md, flags=re.MULTILINE)

        i = 0
        while i < len(sections):
            if sections[i].startswith("##") or sections[i].startswith("###"):
                heading = sections[i + 1].strip()
                body = sections[i + 2] if i + 2 < len(sections) else ""

                # Clean text: remove code blocks, inline code, links, markdown syntax
                clean = re.sub(r"```[\s\S]*?```", "", body)
                clean = re.sub(r"`[^`]+`", "", clean)
                clean = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", clean)
                clean = re.sub(r"[#>*_\-\[\]!]", "", clean)
                clean = re.sub(r"\n+", " ", clean).strip()

                excerpt = clean[:200] if clean else ""

                # Generate heading ID (same as Pandoc's default slug)
                hid = re.sub(r"[^\w\s-]", "", heading.lower()).strip()
                hid = re.sub(r"\s+", "-", hid)

                entries.append({
                    "file": name,
                    "pageTitle": title,
                    "heading": heading,
                    "headingId": hid,
                    "excerpt": excerpt,
                })

                i += 3
            else:
                i += 1

    return entries


def write_json(entries, output_path):
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(entries, f, indent=2, ensure_ascii=False)


def write_js(entries, output_path):
    """Write compact JS with short keys for smaller file size.

    Keys: f=file, p=pageTitle, h=heading, i=headingId, e=excerpt
    """
    os.makedirs(os.path.dirname(output_path), exist_ok=True)

    with open(output_path, "w", encoding="utf-8") as f:
        f.write("window.SEARCH_INDEX=[\n")
        for entry in entries:
            f.write(
                '{f:"' + entry["file"] +
                '",p:"' + _js_escape(entry["pageTitle"]) +
                '",h:"' + _js_escape(entry["heading"]) +
                '",i:"' + entry["headingId"] +
                '",e:"' + _js_escape(entry["excerpt"]) +
                '"},\n'
            )
        f.write("];\n")


def _js_escape(s):
    """Escape characters that would break JS strings."""
    s = s.replace("\\", "\\\\")
    s = s.replace('"', '\\"')
    s = s.replace("\n", " ")
    s = s.replace("\r", " ")
    s = s.replace("\t", " ")
    return s


if __name__ == "__main__":
    if len(sys.argv) < 4:
        print("Usage: generate-search-index.py <format> <output> <file1.md> [file2.md ...]")
        print("  format: json or js")
        sys.exit(1)

    fmt = sys.argv[1]
    output_path = sys.argv[2]
    src_files = sys.argv[3:]

    entries = extract_entries(src_files)

    if fmt == "json":
        write_json(entries, output_path)
    elif fmt == "js":
        write_js(entries, output_path)
    else:
        print(f"Unknown format: {fmt}")
        sys.exit(1)
