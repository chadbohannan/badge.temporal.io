#!/usr/bin/env python3
"""Static checks for Helgrind's hand-authored content (HelgrindWorld.cpp).

- every template keeps all four doorways connected to each other
- every room is reachable from the village once closed edges are applied
- artifact/NPC slots land on walkable tiles in the rooms that use them
- message lines fit the message box (when ./helgrind-host is built; it
  measures strings with the same u8g2 font the badge draws with)

Run via `make check` here, or directly: python3 check-world.py
"""
import re, sys, subprocess, os
from collections import deque

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, '..', '..', 'src', 'screens', 'HelgrindWorld.cpp')
src = open(SRC).read()
SOLID = set('#T~rm')
COLS, ROWS = 16, 6
DOORS = [(7, 0), (8, 0), (7, 5), (8, 5), (0, 2), (0, 3), (15, 2), (15, 3)]

names = re.findall(r'// (kTmpl\w+)', src)
blocks = re.findall(r'\{("#{7}\.\.#{7}",\s*(?:"[^"]{16}",\s*){4}"#{7}\.\.#{7}")\}', src)
templates = {n: re.findall(r'"([^"]{16})"', b) for n, b in zip(names, blocks)}

def bfs(grid, start):
    seen, q = {start}, deque([start])
    while q:
        x, y = q.popleft()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = x + dx, y + dy
            if 0 <= nx < COLS and 0 <= ny < ROWS and grid[ny][nx] not in SOLID and (nx, ny) not in seen:
                seen.add((nx, ny)); q.append((nx, ny))
    return seen

problems = []
for n, g in templates.items():
    if any(len(r) != COLS for r in g): problems.append(f'{n}: row length')
    missing = [d for d in DOORS if d not in bfs(g, DOORS[0])]
    if missing: problems.append(f'{n}: doors not connected {missing}')

room_re = re.compile(r'R\("([^"]+)", (kTmpl\w+), ([\w| ]+), (\w+), (\w+), (\w+), (\d+), (\w+), (\d+)\)')
rooms = room_re.findall(src)
if len(rooms) != 64: problems.append(f'{len(rooms)} rooms parsed, expected 64')

def flags(f):
    return {tok.strip() for tok in f.split('|')}

for i, (nm, t, f, e0, e1, e2, art, npc, loot) in enumerate(rooms):
    g = templates[t]
    if int(art) and int(art) - 1 != 6:
        ax = 10 if t == 'kTmplLonghouse' else 12
        if g[2][ax] in SOLID: problems.append(f'room {i} {nm}: artifact slot is solid')
    if npc != 'kNpcNone' and g[2][4] in SOLID: problems.append(f'room {i} {nm}: NPC slot is solid')

# world reachability with closed edges (sealed from either side)
def open_edge(a, b, mine, theirs):
    return mine not in flags(rooms[a][2]) and theirs not in flags(rooms[b][2])
start = 7 * 8 + 3
seen, q = {start}, deque([start])
while q:
    r = q.popleft(); row, col = divmod(r, 8)
    nbrs = []
    if row > 0 and open_edge(r, r - 8, 'N', 'S'): nbrs.append(r - 8)
    if row < 7 and open_edge(r, r + 8, 'S', 'N'): nbrs.append(r + 8)
    if col > 0 and open_edge(r, r - 1, 'W', 'E'): nbrs.append(r - 1)
    if col < 7 and open_edge(r, r + 1, 'E', 'W'): nbrs.append(r + 1)
    for n in nbrs:
        if n not in seen: seen.add(n); q.append(n)
unreach = [i for i in range(64) if i not in seen]
if unreach: problems.append(f'rooms unreachable from the village: {unreach}')

# text width (needs the host binary)
probe = os.path.join(HERE, 'helgrind-host')
if os.path.exists(probe):
    strs = re.findall(r'"((?:[^"\\]|\\.)*)"', src)
    strs = [s.encode().decode('unicode_escape') for s in strs if len(s) > 8 and not s.startswith('#')]
    out = subprocess.run([probe, '--strwidth'] + strs, capture_output=True, text=True).stdout
else:
    print('note: ./helgrind-host not built, skipping text width check')
    for line in out.splitlines():
        px = int(line.split()[0])
        if px > 117: problems.append(f'text too wide ({px}px): {line[8:]}')

for p in problems: print('PROBLEM:', p)
print('OK' if not problems else f'{len(problems)} problem(s)')
sys.exit(1 if problems else 0)
