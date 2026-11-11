#!/usr/bin/env python3
"""Desk console for the door sign: a wedge shell, a base plate, a hole-size test.

Change the numbers below after measuring the real parts, then re-run.
All sizes are in mm. Case axes: X = left to right, Y = front to rear, Z = up.
"""
import math
import pathlib

import cadquery as cq

# ---- Measure these -------------------------------------------------------
BOARD_L = 63.0        # ESP32 board length (USB end to antenna end)
BOARD_W = 28.0        # ESP32 board width
BUTTON_HOLE = 28.0    # M28 thread; 28 mm fit best on the test plate
BUTTON_BODY_D = 30.0  # threaded body plus a little room
BUTTON_FLANGE_D = 33.3  # sits on top of the case
NUT_D = 37.0          # hex nut across corners (scaled from the drawing)
NUT_ROOM = 10.0       # depth under the panel kept clear for the nut and fingers
BUTTON_BELOW = 33.3   # 28.6 thread + 4.7 terminals, from the drawing
WIRE_ROOM = 8.0       # soldered wires bent sideways off the terminals
BUTTON_DEPTH = BUTTON_BELOW + WIRE_ROOM
# ---- Case shape ----------------------------------------------------------
W, D = 80.0, 80.0     # outer width and depth
H_FRONT = 34.0        # height at the front edge
ANGLE = 15.0          # slope of the top
WALL = 2.8            # also the panel thickness at the button
BASE_T = 2.4          # base plate thickness
BUTTON_X, BUTTON_Y = 24.0, 50.0
LEDGE_H = 3.0         # gap under the board for solder joints
FIT = 0.3             # clearance around the base plate
PCB_T = 1.6
LABEL = ["DO NOT", "DISTURB"]
LABEL_FONT = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"
# --------------------------------------------------------------------------

OUT = pathlib.Path(__file__).resolve().parent
A = math.radians(ANGLE)
NORMAL = (0, -math.sin(A), math.cos(A))
H_REAR = H_FRONT + D * math.tan(A)
COL_R = 4.0
COL_IN = WALL + 3.2
BOARD_CENTER_X = W - WALL - FIT - 3.0 - (BOARD_W + 0.5) / 2
SCREWS = [
    (COL_IN, COL_IN),
    (W - COL_IN, COL_IN),
    (COL_IN, D - COL_IN),
    (BOARD_CENTER_X - (BOARD_W + 0.5) / 2 - 2.0 - COL_R - 1.0, D - COL_IN),
]


def top_z(y):
    return H_FRONT + y * math.tan(A)


def top_plane(x, y, lift=0.0):
    origin = (x + 0, y + NORMAL[1] * lift, top_z(y) + NORMAL[2] * lift)
    return cq.Plane(origin=origin, xDir=(1, 0, 0), normal=NORMAL)


def outer_solid():
    profile = cq.Workplane("YZ").polyline([(0, 0), (D, 0), (D, H_REAR), (0, H_FRONT)]).close()
    body = profile.extrude(W)
    body = body.edges("|Z").fillet(6.0)
    return body.faces(cq.selectors.DirectionMinMaxSelector(cq.Vector(*NORMAL))).edges().fillet(2.0)


def make_shell():
    outer = outer_solid()
    shell = outer.faces("<Z").shell(-WALL)

    columns = None
    for x, y in SCREWS:
        col = cq.Workplane("XY").workplane(offset=BASE_T).center(x, y).circle(COL_R).extrude(H_REAR)
        columns = col if columns is None else columns.union(col)
    shell = shell.union(columns.intersect(outer))

    for x, y in SCREWS:  # pilot holes for M3 screws
        pilot = cq.Workplane("XY").workplane(offset=BASE_T).center(x, y).circle(1.3).extrude(12)
        shell = shell.cut(pilot)

    hole = cq.Workplane(top_plane(BUTTON_X, BUTTON_Y, 1)).circle(BUTTON_HOLE / 2).extrude(-WALL - 3)
    shell = shell.cut(hole)
    # Keep the columns out of the button's way.
    clear = cq.Workplane(top_plane(BUTTON_X, BUTTON_Y, -WALL)).circle(BUTTON_BODY_D / 2 + 0.5).extrude(-BUTTON_DEPTH)
    shell = shell.cut(clear)
    nut = cq.Workplane(top_plane(BUTTON_X, BUTTON_Y, -WALL)).circle(NUT_D / 2 + 1.5).extrude(-NUT_ROOM)
    shell = shell.cut(nut)

    # Opening for the USB ports.
    z0 = BASE_T + 1.0
    usb = (cq.Workplane("XZ").workplane(offset=-D - 1).center(BOARD_CENTER_X, z0 + 5.5)
           .rect(24, 11).extrude(WALL + 3).edges("|Y").fillet(2.0))
    shell = shell.cut(usb)

    # Engraved label, 0.6 deep, beside the button.
    label_x = (BUTTON_X + BUTTON_FLANGE_D / 2 + 3 + W - 5) / 2
    for text, y in zip(LABEL, (BUTTON_Y + 6, BUTTON_Y - 6)):
        letters = (cq.Workplane(top_plane(label_x, y))
                   .text(text, 7.0, -0.6, combine=False, fontPath=LABEL_FONT, halign="center", valign="center"))
        shell = shell.cut(letters)
    return shell


def make_base():
    x0, y0 = WALL + FIT, WALL + FIT
    length_x, length_y = W - 2 * x0, D - 2 * y0
    base = (cq.Workplane("XY").center(W / 2, D / 2).rect(length_x, length_y).extrude(BASE_T)
            .edges("|Z").fillet(3.0))

    # Cradle for the board, open at the rear so the USB ports reach the wall.
    pocket_w = BOARD_W + 0.5
    pocket_l = BOARD_L + 0.5
    rear = D - y0
    front = D - WALL - pocket_l
    cradle_h = LEDGE_H + PCB_T + 1.0
    cy = (front - 2.0 + rear) / 2
    cradle = (cq.Workplane("XY").workplane(offset=BASE_T).center(BOARD_CENTER_X, cy)
              .rect(pocket_w + 4, rear - front + 2.0).extrude(cradle_h))
    pocket = (cq.Workplane("XY").workplane(offset=BASE_T + LEDGE_H).center(BOARD_CENTER_X, (front + rear + 2) / 2)
              .rect(pocket_w, rear - front + 2).extrude(cradle_h))
    under = (cq.Workplane("XY").workplane(offset=BASE_T).center(BOARD_CENTER_X, (front + 2 + rear + 2) / 2)
             .rect(pocket_w - 3, rear - front).extrude(cradle_h))
    outline = (cq.Workplane("XY").center(W / 2, D / 2).rect(length_x, length_y).extrude(30)
               .edges("|Z").fillet(3.0))
    base = base.union(cradle.cut(pocket).cut(under).intersect(outline))

    for x, y in SCREWS:
        screw = cq.Workplane("XY").center(x, y).circle(1.7).extrude(BASE_T)
        head = cq.Workplane("XY").center(x, y).circle(3.2).workplane(offset=1.7).circle(1.7).loft()
        base = base.cut(screw).cut(head)
    return base


def make_test_plate():
    sizes = [28.0, 28.5, 29.0, 30.0]
    plate = cq.Workplane("XY").box(sum(s + 6 for s in sizes) + 4, 42, WALL, centered=(False, False, False))
    x = 2.0
    for s in sizes:
        cx = x + (s + 6) / 2
        plate = plate.cut(cq.Workplane("XY").center(cx, 17).circle(s / 2).extrude(WALL))
        label = (cq.Workplane("XY").workplane(offset=WALL).center(cx, 37)
                 .text(f"{s:g}", 4.5, -0.6, combine=False, fontPath=LABEL_FONT))
        plate = plate.cut(label)
        x += s + 6
    return plate


def button_envelope():
    return cq.Workplane(top_plane(BUTTON_X, BUTTON_Y)).circle(BUTTON_BODY_D / 2).extrude(-BUTTON_DEPTH)


def board_envelope():
    y1 = D - WALL
    z = BASE_T + LEDGE_H
    return (cq.Workplane("XY").workplane(offset=z).center(BOARD_CENTER_X, y1 - BOARD_L / 2)
            .rect(BOARD_W, BOARD_L).extrude(PCB_T + 6))  # parts on top of the board


def overlap(a, b):
    try:
        return a.intersect(b).val().Volume()
    except Exception:
        return 0.0


def check(shell, base):
    button, board = button_envelope(), board_envelope()
    # The button is meant to pass through the top, so only test below it.
    below_top = cq.Workplane(top_plane(BUTTON_X, BUTTON_Y, -WALL - 0.05)).circle(BUTTON_BODY_D / 2).extrude(-BUTTON_DEPTH)
    nut = cq.Workplane(top_plane(BUTTON_X, BUTTON_Y, -WALL - 0.05)).circle(NUT_D / 2).extrude(-NUT_ROOM)
    results = {
        "nut vs shell": overlap(nut, shell),
        "base vs shell": overlap(base, shell),
        "button vs shell": overlap(below_top, shell),
        "button vs base": overlap(button, base),
        "button vs board": overlap(button, board),
        "board vs shell": overlap(board, shell),
        "board vs base": overlap(board, base),
    }
    for name, volume in results.items():
        print(f"{name:18s} {volume:8.2f} mm3 {'OK' if volume < 0.01 else 'COLLISION'}")
    lowest = button.val().BoundingBox().zmin
    print(f"button lowest point z={lowest:.1f}, floor z={BASE_T}, gap {lowest - BASE_T:.1f}")
    print(f"size {W} x {D}, height {H_FRONT} front to {H_REAR:.1f} rear")


if __name__ == "__main__":
    shell, base, plate = make_shell(), make_base(), make_test_plate()
    check(shell, base)
    # Print the shell with its top face on the bed: no supports, smooth top.
    flipped = shell.rotate((0, 0, 0), (1, 0, 0), 180 - ANGLE)
    flipped = flipped.translate((0, 0, -flipped.val().BoundingBox().zmin))
    cq.exporters.export(flipped, str(OUT / "case-top-print-this-way-up.stl"), tolerance=0.02, angularTolerance=0.1)
    cq.exporters.export(base, str(OUT / "case-base.stl"), tolerance=0.02, angularTolerance=0.1)
    cq.exporters.export(plate, str(OUT / "button-hole-test.stl"), tolerance=0.02, angularTolerance=0.1)
    cq.exporters.export(shell, str(OUT / "case-top.step"))
    cq.exporters.export(base, str(OUT / "case-base.step"))
    bb = flipped.val().BoundingBox()
    print(f"print size {bb.xlen:.1f} x {bb.ylen:.1f} x {bb.zlen:.1f}")
