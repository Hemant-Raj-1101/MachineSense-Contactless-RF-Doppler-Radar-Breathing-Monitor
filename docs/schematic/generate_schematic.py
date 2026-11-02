#!/usr/bin/env python3
"""Generate the MachineSense analog front-end schematic (PNG, SVG, PDF).

Usage:  python3 generate_schematic.py
Output: ../images/schematic.{png,svg,pdf}

Requires: matplotlib, numpy
"""
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.patches import Circle, Polygon, Rectangle

INK = "#111111"
GREY = "#555555"
LW = 2.2
FONT = "DejaVu Sans"

fig = plt.figure(figsize=(17.4, 7.9))
ax = fig.add_axes([0, 0, 1, 1])
X0, X1, Y0, Y1 = 0.0, 34.8, -0.9, 15.6
ax.set_xlim(X0, X1)
ax.set_ylim(Y0, Y1)
ax.set_aspect("equal")
ax.axis("off")


# ---------------------------------------------------------------- primitives
def wire(*pts):
    xs, ys = zip(*pts)
    ax.plot(xs, ys, color=INK, lw=LW, solid_capstyle="round",
            solid_joinstyle="round", zorder=2)


def dot(x, y):
    ax.plot([x], [y], "o", ms=8, color=INK, zorder=5)


def text(x, y, s, size=13, weight="normal", ha="center", va="center",
         color=INK, style="normal", family=FONT):
    ax.text(x, y, s, fontsize=size, fontweight=weight, ha=ha, va=va,
            color=color, fontstyle=style, family=family, zorder=6)


def resistor(cx, cy, L=1.6, h=0.30, vertical=False):
    f = np.array([0, 1 / 12, 3 / 12, 5 / 12, 7 / 12, 9 / 12, 11 / 12, 1])
    a = (f - 0.5) * L
    b = h * np.array([0, 1, -1, 1, -1, 1, -1, 0])
    if vertical:
        ax.plot(cx + b, cy + a, color=INK, lw=LW, solid_joinstyle="miter", zorder=3)
        return cy - L / 2, cy + L / 2
    ax.plot(cx + a, cy + b, color=INK, lw=LW, solid_joinstyle="miter", zorder=3)
    return cx - L / 2, cx + L / 2


def capacitor(cx, cy, gap=0.30, half=0.55, vertical=False):
    g = gap / 2
    if vertical:
        for d in (-g, g):
            ax.plot([cx - half, cx + half], [cy + d, cy + d], color=INK, lw=LW + 0.6,
                    solid_capstyle="butt", zorder=3)
        return cy - g, cy + g
    for d in (-g, g):
        ax.plot([cx + d, cx + d], [cy - half, cy + half], color=INK, lw=LW + 0.6,
                solid_capstyle="butt", zorder=3)
    return cx - g, cx + g


def ground(x, y, w=0.8):
    """Ground symbol whose terminal is at (x, y)."""
    for i, k in enumerate((1.0, 0.62, 0.25)):
        yy = y - i * 0.17
        ax.plot([x - w / 2 * k, x + w / 2 * k], [yy, yy], color=INK, lw=LW,
                solid_capstyle="round", zorder=3)


def supply_bar(x, y, w=1.0):
    ax.plot([x - w / 2, x + w / 2], [y, y], color=INK, lw=LW + 0.8,
            solid_capstyle="round", zorder=3)


def source(cx, cy, r=0.8):
    ax.add_patch(Circle((cx, cy), r, fc="white", ec=INK, lw=LW, zorder=3))
    ax.plot([cx - 0.18, cx + 0.18], [cy + 0.42, cy + 0.42], color=INK, lw=1.6, zorder=4)
    ax.plot([cx, cx], [cy + 0.24, cy + 0.60], color=INK, lw=1.6, zorder=4)
    ax.plot([cx - 0.18, cx + 0.18], [cy - 0.42, cy - 0.42], color=INK, lw=1.6, zorder=4)
    # AC sine glyph
    t = np.linspace(-0.4, 0.4, 40)
    ax.plot(cx + t, cy + 0.17 * np.sin(t / 0.4 * 2 * np.pi), color=INK, lw=1.5, zorder=4)


def opamp(xl, xt, yc, hh=1.3):
    ax.add_patch(Polygon([(xl, yc + hh), (xl, yc - hh), (xt, yc)], closed=True,
                         fc="white", ec=INK, lw=LW + 0.4, joinstyle="miter", zorder=3))
    text(xl + 0.30, yc + 0.7, "−", size=15, weight="bold")
    text(xl + 0.30, yc - 0.7, "+", size=15, weight="bold")


def opamp_supply(xl, xt, yc, hh, xp):
    """Draw V+ (top) and V- (bottom) supply stubs at x = xp on the slanted edges."""
    k = (xp - xl) / (xt - xl)
    y_top = yc + hh - k * hh
    y_bot = yc - hh + k * hh
    # V+ : stub up to a supply bar
    wire((xp, y_top), (xp, y_top + 0.85))
    supply_bar(xp, y_top + 0.85)
    text(xp, y_top + 1.22, "V+ = 3.3 V", size=12.5, weight="bold")
    # V- : stub down to ground
    wire((xp, y_bot), (xp, y_bot - 0.85))
    ground(xp, y_bot - 0.85)
    text(xp, y_bot - 1.55, "V− = GND", size=12.5, weight="bold")


def label(x, y_top, name, value, ha="center"):
    text(x, y_top, name, size=14, weight="bold", ha=ha)
    text(x, y_top - 0.55, value, size=12.5, ha=ha)


# --------------------------------------------------------------- title block
ax.add_patch(Rectangle((0.25, -0.65), X1 - 0.5, Y1 - 0.25 + 0.65 - 0.2, fill=False,
                       ec=INK, lw=1.2, zorder=1))
text(X1 / 2, 14.45, "MachineSense: Non-Contact RF Vital Sign Monitor", size=24, weight="bold")
text(X1 / 2, 13.65, "Analog front-end: two-stage inverting LM358 band-pass amplifier, "
     "total gain 10,000 (single 3.3 V supply)", size=13, color=GREY)

# ---------------------------------------------------------------- input stage
Y = 7.7          # stage-1 main signal line
# V4 test source (HB100 IF output in simulation)
source(2.6, 6.1)
wire((2.6, 6.9), (2.6, Y), (4.55, Y))
wire((2.6, 5.3), (2.6, 4.75))
ground(2.6, 4.75)
text(2.6, 9.55, "V4", size=14, weight="bold")
text(2.6, 8.95, "AC 1", size=12.5)
text(2.6, 8.40, "SINE(0 10u 0.3)", size=12.5)

# C1
xl, xr = capacitor(5.0, Y)
wire((4.55, Y), (xl, Y)); wire((xr, Y), (6.8, Y))
text(5.0, 8.7, "C1", size=14, weight="bold")
text(5.0, 6.75, "147µ", size=12.5)

# R3
a, b = resistor(7.6, Y)
wire((xr, Y), (a, Y)); wire((b, Y), (9.4, Y), (10.4, Y))
text(7.6, 8.7, "R3", size=14, weight="bold")
text(7.6, 6.75, "10k", size=12.5)
dot(9.4, Y)

# ------------------------------------------------------------------- U1
XL1, XT1, YC1, HH = 10.4, 12.8, 7.0, 1.3
opamp(XL1, XT1, YC1, HH)
opamp_supply(XL1, XT1, YC1, HH, 11.6)
text(13.15, 6.15, "U1", size=14, weight="bold", ha="left")
text(13.15, 5.65, "LM358", size=12, ha="left", color=GREY)
wire((XT1, YC1), (16.2, YC1))
dot(16.2, YC1)

# U1 feedback network  R4 || C2
wire((9.4, Y), (9.4, 11.4), (13.6, 11.4))
dot(11.2, 11.4)
ya, yb = resistor(11.2, 10.65, L=1.2, vertical=True)
wire((11.2, 11.4), (11.2, yb)); wire((11.2, ya), (11.2, 9.9))
ya2, yb2 = capacitor(13.6, 10.65, vertical=True)
wire((13.6, 11.4), (13.6, yb2)); wire((13.6, ya2), (13.6, 9.9))
wire((11.2, 9.9), (16.2, 9.9), (16.2, YC1))
dot(13.6, 9.9)
text(11.65, 10.95, "R4", size=14, weight="bold", ha="left")
text(11.65, 10.40, "1Meg", size=12.5, ha="left")
text(14.40, 10.95, "C2", size=14, weight="bold", ha="left")
text(14.40, 10.40, "0.044µ", size=12.5, ha="left")
text(12.6, 12.5, "STAGE 1  •  A₁ = −R4/R3 = −100", size=11.5,
     color=GREY, style="italic")

# ------------------------------------------------------------ inter-stage
Y2 = 7.0
xl, xr = capacitor(17.5, Y2)
wire((16.2, Y2), (xl, Y2))
text(17.5, 7.95, "C3", size=14, weight="bold")
text(17.5, 6.05, "147µ", size=12.5)
a, b = resistor(19.9, Y2)
wire((xr, Y2), (a, Y2)); wire((b, Y2), (22.7, Y2))
text(19.9, 7.95, "R5", size=14, weight="bold")
text(19.9, 6.05, "10k", size=12.5)
dot(21.7, Y2)

# ------------------------------------------------------------------- U2
XL2, XT2, YC2 = 22.7, 25.1, 6.3
opamp(XL2, XT2, YC2, HH)
opamp_supply(XL2, XT2, YC2, HH, 23.9)
text(25.45, 5.45, "U2", size=14, weight="bold", ha="left")
text(25.45, 4.95, "LM358", size=12, ha="left", color=GREY)
wire((XT2, YC2), (28.3, YC2))
dot(28.3, YC2)

# U2 feedback network R6 || C4
wire((21.7, Y2), (21.7, 10.7), (25.9, 10.7))
dot(23.5, 10.7)
ya, yb = resistor(23.5, 9.95, L=1.2, vertical=True)
wire((23.5, 10.7), (23.5, yb)); wire((23.5, ya), (23.5, 9.2))
ya2, yb2 = capacitor(25.9, 9.95, vertical=True)
wire((25.9, 10.7), (25.9, yb2)); wire((25.9, ya2), (25.9, 9.2))
wire((23.5, 9.2), (28.3, 9.2), (28.3, YC2))
dot(25.9, 9.2)
text(23.95, 10.25, "R6", size=14, weight="bold", ha="left")
text(23.95, 9.70, "1Meg", size=12.5, ha="left")
text(26.65, 10.25, "C4", size=14, weight="bold", ha="left")
text(26.65, 9.70, "0.044µ", size=12.5, ha="left")
text(24.7, 12.5, "STAGE 2  •  A₂ = −R6/R5 = −100", size=11.5,
     color=GREY, style="italic")

# ---------------------------------------------------------------- output
wire((28.3, YC2), (29.3, YC2))
ax.add_patch(Polygon([(29.3, 5.7), (33.0, 5.7), (33.8, 6.3), (33.0, 6.9), (29.3, 6.9)],
                     closed=True, fc="white", ec=INK, lw=LW + 0.4, zorder=3))
text(31.25, 6.3, "ESP32 (GPIO 34)", size=13, weight="bold")

# ------------------------------------------------- V_REF divider (bias)
XD = 6.0
supply_bar(XD, 5.25)
text(XD, 5.65, "+3.3 V", size=12.5, weight="bold")
wire((XD, 5.25), (XD, 4.6))
resistor(XD, 4.0, L=1.2, vertical=True)
wire((XD, 3.4), (XD, 2.7))
dot(XD, 2.7)
wire((XD, 2.7), (XD, 2.2))
resistor(XD, 1.6, L=1.2, vertical=True)
wire((XD, 1.0), (XD, 0.55))
ground(XD, 0.55)
text(XD + 0.5, 4.30, "R1", size=14, weight="bold", ha="left")
text(XD + 0.5, 3.75, "10k", size=12.5, ha="left")
text(XD + 0.5, 1.90, "R2", size=14, weight="bold", ha="left")
text(XD + 0.5, 1.35, "10k", size=12.5, ha="left")

# bias bus -> (+) inputs
wire((XD, 2.7), (21.2, 2.7))
wire((8.9, 2.7), (8.9, 6.3), (XL1, 6.3))
dot(8.9, 2.7)
wire((21.2, 2.7), (21.2, 5.6), (XL2, 5.6))
dot(21.2, 2.7)
text(15.0, 3.15, "V$_{\\mathrm{REF}}$ = 1.65 V  (mid-supply bias)", size=12.5, weight="bold")

# --------------------------------------------------------------- notes
notes = [
    "Midband gain per stage |A| = R$_f$ / R$_{in}$ = 1 MΩ / 10 kΩ = 100;  "
    "total A = 100 × 100 = 10$^4$",
    "High-pass corner  f$_{HP}$ = 1/(2πR3C1) ≈ 0.108 Hz        "
    "Low-pass corner  f$_{LP}$ = 1/(2πR4C2) ≈ 3.62 Hz",
    "SPICE:  .ac dec 100 0.01 10     .tran 10",
]
for i, n in enumerate(notes):
    text(10.0, 1.55 - 0.62 * i, n, size=11, ha="left", color=GREY)

out = Path(__file__).resolve().parent.parent / "images"
out.mkdir(exist_ok=True)
fig.savefig(out / "schematic.png", dpi=300, facecolor="white")
fig.savefig(out / "schematic.svg", facecolor="white")
fig.savefig(out / "schematic.pdf", facecolor="white")
print("written to", out)
