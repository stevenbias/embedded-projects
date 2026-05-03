#!/usr/bin/env python3
"""Generate sidebar HTML from SRCS markdown files."""
import sys, re, os
from collections import defaultdict

PHASES = {
    0: "Getting Started",
    1: "Phase 1: Bare Metal",
    2: "Phase 2: Peripherals",
    3: "Phase 3: Architecture",
    4: "Phase 4: Real-Time",
    5: "Phase 5: Expert",
}

def html_escape(s):
    return s.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')

def get_meta(filepath):
    with open(filepath) as f:
        m = re.search(r'^---\s*\n(.*?)\n---', f.read(), re.DOTALL)
    if not m:
        return None, None
    fm = m.group(1)
    title_m = re.search(r'^title:\s*(.+)$', fm, re.MULTILINE)
    phase_m = re.search(r'^phase:\s*(\d+)$', fm, re.MULTILINE)
    if not title_m or not phase_m:
        return None, None
    title = title_m.group(1).strip().strip('"')
    # Extract short title: use part before first ':' or full title
    short = title.split(':')[0].strip()
    return html_escape(short), int(phase_m.group(1))

# Group by phase (preserving SRCS order)
phases = defaultdict(list)
for f in sys.argv[1:]:
    title, phase = get_meta(f)
    if title is not None and phase is not None:
        name = os.path.splitext(os.path.basename(f))[0]
        phases[phase].append((name, title))

# Output HTML
print('  <div class="sidebar-content">')
for pnum in sorted(phases.keys()):
    print(f'    <div class="sidebar-section">')
    print(f'      <h3>{PHASES.get(pnum, f"Phase {pnum}")}</h3>')
    print('      <ul>')
    for name, title in phases[pnum]:
        print(f'        <li><a href="{name}.html">{title}</a></li>')
    print('      </ul>')
    print('    </div>')
print('  </div>')
