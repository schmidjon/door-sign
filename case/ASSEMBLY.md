# Door sign console: build notes

## Button sizes used
- Hole: 28 mm (best fit on the test plate; the button has an M28 thread).
- Below the top: 28.6 mm thread + 4.7 mm terminals = 33.3 mm, from the maker's drawing,
  plus 8 mm for soldered wires bent sideways.
- The hex nut (about 37 mm across) has 10 mm of clear space under the top.

## Printing
- `case-top-print-this-way-up.stl`: already rotated so the top lies flat on the bed. No supports needed.
- `case-base.stl`: print flat, cradle facing up. No supports needed.

## Parts
- EG STARTS 30mm 5V LED button
- NPN transistor (2N3904 or PN2222)
- 1 kOhm resistor
- 4 x M3 x 10 mm countersunk screws, which cut their own thread in the case's 2.6 mm holes
- Solder the wires to the button tabs and bend them sideways (push-on spade connectors are too long to fit)
- Stick-on rubber feet, double-sided foam tape for the board

## Wiring (soldered to the board)
Check the board's 5V pin with a meter first: with USB connected it should read about 5V.

| From | To |
|---|---|
| Switch terminal 1 | GPIO 4 |
| Switch terminal 2 | GND |
| Board 5V | LED + |
| LED - | Transistor collector |
| Transistor emitter | GND |
| GPIO 5 | 1 kOhm resistor, then transistor base |

Check your transistor's pin order on its datasheet, because it varies between parts.
Find the LED's + side by testing it first. Only use 5V if the listing confirms a built-in
resistor; otherwise add one in series.

Insulate the transistor and resistor legs with heat-shrink so nothing shorts inside the case.

## Assembly
1. Push the button through the top and tighten its nut from inside by hand.
2. Wire everything, then slide the board into the cradle with its USB ports at the rear.
3. Stick the board down with a little foam tape.
4. Fit the base and screw it into the four posts from underneath.
5. Add rubber feet to the base, away from the screw heads.
