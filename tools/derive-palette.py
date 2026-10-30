#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Derive Kalipso's colours instead of picking them by hand.

Method (documented in design/palette.md, which this script writes):
1. Neutral tint: the hue of the warm paper backgrounds measured on reference sites.
2. One neutral ramp in OKLCH (perceptual lightness): each step's lightness is solved for the
   contrast its role needs (text 4.5:1, control borders 3:1), not chosen by eye.
3. The "you are here" accent: every hue is scored for vividness at the lightness needed on paper
   and on night, distance from error red under protanopia, deuteranopia and tritanopia, and
   distance from warning yellow-orange; the best hue wins.
4. Error red uses the same lightness solving.
Writes the values into design/tokens.json (group "site") and the report into design/palette.md.
"""
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# --- colour maths: OKLab/OKLCH (Björn Ottosson) and WCAG 2 contrast -------------------------
def srgb_to_linear(c): return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
def linear_to_srgb(c): return 12.92 * c if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055

def hex_to_linear(h):
    h = h.lstrip('#')
    return [srgb_to_linear(int(h[i:i + 2], 16) / 255) for i in (0, 2, 4)]

def linear_to_oklab(rgb):
    r, g, b = rgb
    l = 0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b
    m = 0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b
    s = 0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b
    l, m, s = (math.copysign(abs(v) ** (1 / 3), v) for v in (l, m, s))
    return (0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
            1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
            0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s)

def oklch_to_linear(L, C, h):
    a, b = C * math.cos(math.radians(h)), C * math.sin(math.radians(h))
    l = (L + 0.3963377774 * a + 0.2158037573 * b) ** 3
    m = (L - 0.1055613458 * a - 0.0638541728 * b) ** 3
    s = (L - 0.0894841775 * a - 1.2914855480 * b) ** 3
    return [4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
            -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
            -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s]

def in_gamut(rgb): return all(-1e-6 <= c <= 1 + 1e-6 for c in rgb)

def to_hex(rgb):
    return '#' + ''.join(f'{round(max(0, min(1, linear_to_srgb(c))) * 255):02X}' for c in rgb)

def luminance(rgb): return 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2]

def contrast(a, b):
    x, y = luminance(hex_to_linear(a)), luminance(hex_to_linear(b))
    return (max(x, y) + 0.05) / (min(x, y) + 0.05)

def oklch_of(hex_):
    L, a, b = linear_to_oklab(hex_to_linear(hex_))
    return L, math.hypot(a, b), math.degrees(math.atan2(b, a)) % 360

def max_chroma(L, h):
    lo, hi = 0.0, 0.4
    for _ in range(30):
        mid = (lo + hi) / 2
        lo, hi = (mid, hi) if in_gamut(oklch_to_linear(L, mid, h)) else (lo, mid)
    return lo

def color(L, C, h):
    return to_hex(oklch_to_linear(L, min(C, max_chroma(L, h)), h))

def solve_lightness(C, h, against, ratio, darker):
    """Lightest (darker=True) or darkest (darker=False) OKLCH lightness meeting `ratio` against
    every colour in `against`, at chroma C (clipped to the gamut)."""
    steps = [i / 1000 for i in range(1000)]
    for L in (reversed(steps) if darker else steps):
        candidate = color(L, C, h)
        if all(contrast(candidate, bg) >= ratio for bg in against):
            return L, candidate
    raise SystemExit(f'no lightness meets {ratio}:1 for hue {h}')

# --- colour vision deficiency (Machado, Oliveira and Fernandes 2009, severity 1) ---------------
CVD = {
    'protanopia': [[0.152286, 1.052583, -0.204868], [0.114503, 0.786281, 0.099216], [-0.003882, -0.048116, 1.051998]],
    'deuteranopia': [[0.367322, 0.860646, -0.227968], [0.280085, 0.672501, 0.047413], [-0.011820, 0.042940, 0.968881]],
    'tritanopia': [[1.255528, -0.076749, -0.178779], [-0.078411, 0.930809, 0.147602], [0.004733, 0.691367, 0.303900]],
}
def simulate(hex_, matrix):
    rgb = hex_to_linear(hex_)
    return [max(0.0, min(1.0, sum(matrix[i][j] * rgb[j] for j in range(3)))) for i in range(3)]

def distance(a_rgb, b_rgb):
    return math.dist(linear_to_oklab(a_rgb), linear_to_oklab(b_rgb))

def worst_cvd_distance(a, b):
    pairs = [(hex_to_linear(a), hex_to_linear(b))] + [(simulate(a, m), simulate(b, m)) for m in CVD.values()]
    return min(distance(x, y) for x, y in pairs)

def hue_gap(a, b): return abs((a - b + 180) % 360 - 180)

# --- 1. evidence: reference paper backgrounds and measured accents -----------------------------
PAPERS = {'Claude Code (Webby 2026)': '#FBFBF9', 'Rijksmuseum Collection (Webby 2025)': '#EFEAE7'}
ACCENTS = {'Google Chrome (PV 2024)': '#1A73E8', 'Reown (Webby 2025)': '#379DEF', 'Affinity (PV 2026)': '#A7F175'}
tint = sum(oklch_of(h)[2] for h in PAPERS.values()) / len(PAPERS)
TINT_C = 0.006  # barely there: warm, never beige

# WCAG 2.2 AA asks 4.5:1 for text and 3:1 for control borders. Values solved exactly at the threshold
# look lighter after anti-aliasing and on screens with a different gamma, so every role gets 10% more.
MARGIN = 1.1
TEXT, CONTROL = 4.5 * MARGIN, 3.0 * MARGIN

# --- 2. neutral ramp -------------------------------------------------------------------------
paper, paper_deep, card = color(0.985, TINT_C, tint), color(0.955, TINT_C + 0.004, tint), '#FFFFFF'
line = color(0.905, TINT_C + 0.004, tint)
_, control = solve_lightness(TINT_C, tint, [paper, paper_deep, card], CONTROL, darker=True)
_, muted = solve_lightness(TINT_C, tint, [paper, paper_deep, card], TEXT, darker=True)
ink, ink_raised = color(0.215, TINT_C, tint), color(0.30, TINT_C, tint)
night, night_raised = color(0.17, TINT_C, tint), color(0.215, TINT_C, tint)
# The app's dark theme also needs a hover tone for controls; every role on night is solved against it too.
night_hover = color(0.26, TINT_C, tint)
DARK = [night, night_raised, night_hover]
night_line = color(0.29, TINT_C, tint)
night_text = color(0.955, TINT_C, tint)
_, night_control = solve_lightness(TINT_C, tint, DARK, CONTROL, darker=False)
_, night_muted = solve_lightness(TINT_C, tint, DARK, TEXT, darker=False)

# --- 4 first: error red (needed to keep the accent away from it) ------------------------------
ERROR_H, WARNING_H = 27.0, 70.0
_, error = solve_lightness(0.2, ERROR_H, [paper, paper_deep, card], TEXT, darker=True)
_, error_night = solve_lightness(0.2, ERROR_H, DARK, TEXT, darker=False)

# --- 3. score every hue for the "you are here" accent ------------------------------------------
reference_hues = [oklch_of(h)[2] for h in ACCENTS.values()]
rows = []
for h in range(0, 360, 5):
    try:
        L_day, day = solve_lightness(0.25, h, [paper, paper_deep, card], TEXT, darker=True)
        L_night, glow = solve_lightness(0.25, h, DARK, TEXT, darker=False)
    except SystemExit:
        continue
    vivid = min(max_chroma(L_day, h), max_chroma(L_night, h))
    separation = min(worst_cvd_distance(day, error), worst_cvd_distance(glow, error_night))
    from_warning = hue_gap(h, WARNING_H)
    near_reference = min(hue_gap(h, r) for r in reference_hues)
    # Vividness and separation carry the weight; hues next to warning orange-yellow or error red are excluded.
    score = (vivid / 0.2) + (separation / 0.15)
    if from_warning < 35 or hue_gap(h, ERROR_H) < 50:
        score = -1
    rows.append((score, h, day, glow, vivid, separation, near_reference))
rows.sort(reverse=True)
# Hues within 3% of the best score are equally good; among them, the one closest to an accent that
# award-winning references use wins (familiar to visitors, and continuous with the app's blue).
TIE = 0.03
tied = [r for r in rows if r[0] >= rows[0][0] * (1 - TIE)]
best = min(tied, key=lambda r: (r[6], -r[0]))
accent_h, accent, accent_night = best[1], best[2], best[3]
accent_soft = color(0.945, 0.035, accent_h)

palette = {
    'paper': paper, 'paperDeep': paper_deep, 'card': card, 'ink': ink, 'inkRaised': ink_raised, 'muted': muted,
    'line': line, 'control': control, 'amber': accent, 'amberSoft': accent_soft,
    'night': night, 'nightRaised': night_raised, 'nightText': night_text, 'nightMuted': night_muted,
    'nightLine': night_line, 'nightControl': night_control, 'amberNight': accent_night,
    'error': error, 'errorNight': error_night, 'nightHover': night_hover,
}

tokens_path = ROOT / 'design/tokens.json'
tokens = json.loads(tokens_path.read_text())
for key in ('amber', 'amberSoft', 'amberNight'):
    tokens['site'].pop(key, None)
renamed = {('accent' + k[5:] if k.startswith('amber') else k): v for k, v in palette.items()}
tokens['site'].update(renamed)
# The app's two themes take the same values by role: ink acts on paper, paper-white acts on night,
# blue marks where you are (current word, marker, progress, focus, selection).
app = {
    'light': {'background': paper, 'text': ink, 'muted': muted, 'surface': card, 'hover': paper_deep,
              'border': control, 'focus': accent, 'accent': accent, 'action': ink, 'actionHover': ink_raised,
              'actionText': paper, 'selection': accent, 'selectionText': card, 'divider': line},
    'dark': {'background': night, 'text': night_text, 'muted': night_muted, 'surface': night_raised,
             'hover': night_hover, 'border': night_control, 'focus': accent_night, 'accent': accent_night,
             'action': night_text, 'actionHover': color(0.86, TINT_C, tint), 'actionText': night,
             'selection': accent_night, 'selectionText': night, 'divider': night_line},
}
# The current word's tint, as on the website (color-mix in sRGB): 24% of the blue over the background.
def mix(a, b, share):
    a, b = a.lstrip('#'), b.lstrip('#')
    return '#' + ''.join(f'{round(int(a[i:i + 2], 16) * share + int(b[i:i + 2], 16) * (1 - share)):02X}' for i in (0, 2, 4))
for theme in app.values():
    theme['tint'] = mix(theme['accent'], theme['background'], 0.24)
tokens['palette'] = app
tokens_path.write_text(json.dumps(tokens, indent=2, ensure_ascii=False) + '\n')

# --- report -----------------------------------------------------------------------------------
def lch(h): L, C, H = oklch_of(h); return f'L {L:.3f} C {C:.3f} h {H:.0f}'
out = ['# Kalipso palette — how it was derived', '',
       'Generated by `tools/derive-palette.py`; rerun it to reproduce. Values feed `design/tokens.json` (group "site").',
       '', '## Evidence', '',
       f'- Warm paper backgrounds measured on reference sites: ' + ', '.join(f'{k} {v} ({lch(v)})' for k, v in PAPERS.items())
       + f'. Neutral tint hue = their mean, **{tint:.0f}°**, at chroma {TINT_C}: warm but never beige.',
       '- Accents measured on reference sites: ' + ', '.join(f'{k} {v} (h {oklch_of(v)[2]:.0f}°)' for k, v in ACCENTS.items())
       + '. Used only to break ties between equally good hues.',
       '- Several winners use a dark neutral as the action colour (Claude Code, Google Antigravity, Tailscale, Shopify): '
       'the filled action stays ink.',
       '', '## Roles and values', '',
       f'Targets: text {TEXT:.2f}:1 and control borders {CONTROL:.1f}:1, WCAG 2.2 AA plus a 10% margin for anti-aliasing '
       'and screen gamma.', '',
       '| Role | Token | Value | OKLCH | Contrast |', '|---|---|---|---|---|']
def row(role, token, value, against):
    ratios = ', '.join(f'{contrast(value, b):.2f}:1 on {n}' for n, b in against)
    out.append(f'| {role} | {token} | `{value}` | {lch(value)} | {ratios} |')
light = [('paper', paper), ('paper-deep', paper_deep), ('card', card)]
dark = [('night', night), ('night-raised', night_raised), ('night-hover', night_hover)]
row('Page', 'paper', paper, [('ink', ink)])
row('Section band', 'paperDeep', paper_deep, [('ink', ink)])
row('Text and filled action', 'ink', ink, light)
row('Secondary text', 'muted', muted, light)
row('Control border', 'control', control, light)
row('Decorative line', 'line', line, [('paper', paper)])
row('"You are here"', 'accent', accent, light)
row('Error', 'error', error, light)
row('Reader and dark band', 'night', night, [('night-text', night_text)])
row('Hover on night (app)', 'nightHover', night_hover, [('night-text', night_text)])
row('Text on night', 'nightText', night_text, dark)
row('Secondary text on night', 'nightMuted', night_muted, dark)
row('Control border on night', 'nightControl', night_control, dark)
row('"You are here" on night', 'accentNight', accent_night, dark)
row('Error on night', 'errorNight', error_night, dark)
row('Current word tint', 'palette.light.tint', app['light']['tint'], [('ink', ink)])
row('Current word tint on night', 'palette.dark.tint', app['dark']['tint'], [('night-text', night_text)])
out += ['', '## App themes', '', 'The app\'s light and dark themes (`palette` in tokens.json) use the same values by role:', '',
        '| Role | Light | Dark |', '|---|---|---|']
for role in app['light']:
    out.append(f'| {role} | `{app["light"][role]}` | `{app["dark"][role]}` |')
out += ['', '## The "you are here" hue', '',
        f'Every hue in 5° steps was given the lightness it needs for {TEXT:.2f}:1 text on paper and on night (4.5:1 plus 10%), then scored '
        f'(vividness / 0.2 + worst separation from error / 0.15). {len(tied)} hues scored within {TIE:.0%} of the best '
        f'and count as equally good; among them the one closest to a reference accent wins: **{accent_h}°**.', '',
        '| Hue | On paper | On night | Vividness (min chroma) | Worst distance from error, incl. colour-blind views | Distance from a reference accent |',
        '|---|---|---|---|---|---|']
for score, h, day, glow, vivid, sep, near in tied:
    out.append(f'| {h}°{" ✓" if h == accent_h else ""} | `{day}` | `{glow}` | {vivid:.3f} | {sep:.3f} | {near:.0f}° |')
out += ['', 'Excluded: hues within 50° of error red and within 35° of warning yellow-orange, so that position, error '
        'and warning are never confused. Distances are Euclidean in OKLab; colour-blind views use the Machado, '
        'Oliveira and Fernandes (2009) matrices at full severity.', '']
(ROOT / 'design/palette.md').write_text('\n'.join(out) + '\n')
print(f'accent {accent_h}° {accent} / {accent_night}; tint {tint:.0f}°; error {error} / {error_night}')
for score, h, day, glow, vivid, sep, near in rows[:5]:
    print(f'  {h:3}° score {score:.2f} vivid {vivid:.3f} sep {sep:.3f} near {near:.0f}')
