# FLIGHT87

A flat-shaded 3D rail-shooter demo for the original IBM PC: **8088 + 8087 + CGA**.
You fly a small delta-wing fighter low over a striped plain, weaving between
spinning cubes, pyramids, pillars, octahedra and arches, and shoot them with lasers.

![demo](docs/demo.gif)

It is a single 10 KB `.COM` file written in NASM assembly. It uses only 8086
instructions (`cpu 8086` is enforced) and 8087 instructions (no 287/387 ops like
`FSIN` or `FSTSW AX`).

## Running

```
FLIGHT87.COM
```

Requires: 8088/8086 (or anything later), an **8087** (it checks and refuses to run
without one), CGA (or compatible), DOS 2+.

| Key | Action |
|---|---|
| Arrows / WASD | fly |
| Space / Ctrl | fire |
| P | toggle CGA palette (cyan/magenta ↔ green/red) |
| Esc | quit |

With no input it runs in an attract mode with an autopilot.

In DOSBox use `machine=cga`. Note that DOSBox does not model 8087 timing, so FPU
instructions are almost free there; the FPS you see in DOSBox is optimistic.

## Building

```
./build.sh
```

Needs `python3` (generates the sine table, dither patterns and models into
`src/data.inc`) and `nasm`.

## How the 8087 is used

The 8087 is not much faster than the 8088 at any single multiply or divide
(both take roughly 100–200 clocks). The demo gets its benefit in three ways.

1. **All the 3D math runs on the 8087, in floating point.** That covers rotation
   matrices from a float sine table, vertex transforms, back-face culling (the
   camera is moved into object space, so each face costs one dot product), Lambert
   shading, perspective divide, Sutherland–Hodgman clipping against the near
   plane and screen edges in 3D, ship physics and the chase camera. There is no
   fixed-point overflow juggling.
2. **The two chips run in parallel.** The 8088 has to `WAIT` before each 8087
   instruction. Here every FPU mnemonic is a macro that emits `WAIT` + opcode,
   as MASM used to do. Integer code placed between two FPU instructions
   therefore runs while the 8087 is still busy. Three places use this on purpose:
   - **Ground:** while the 8087 computes the next stripe boundary
     (`y = H·FY / z`, an FDIV), the 8088 fills the current stripe's rows with
     `REP STOSW`.
   - **Vertices:** while the 8087 does vertex *i*'s perspective FDIV, the 8088
     computes the screen outcode of vertex *i−1*.
   - **Faces:** while the 8087 does a face's lighting, the 8088 marks which
     vertices that face uses (vertices used only by back faces are never
     transformed).
3. **Each chip does the work it is good at.** The 8088 handles integer edge
   walking, dithered span filling, the depth sort, collisions and the blit to
   CGA memory.

Other details:
- Ordered 2×2 dithering turns the 4 CGA colours into 7-level shading ramps.
- The background colour register is the sky, and flashes red when you crash.
- Objects are depth-sorted (painter's algorithm). Convex objects need no
  per-face sort; the ship and the arches sort their faces.
- The PIT runs at 145.6 Hz (chaining the BIOS every 8th tick), so movement is
  frame-rate independent.
- Floats are compared with integer instructions on their bit patterns where
  possible, because the 8087 has no `FSTSW AX` and reading flags costs a
  memory round-trip.

## Performance

`./build.sh -DCOUNT` shows the number of 8087 instructions executed per frame
on the HUD. A typical busy frame runs about 6,000–7,000.

At roughly 100–120 clocks per 8087 instruction on a 4.77 MHz machine
(execution plus `WAIT` plus 8-bit bus fetches), the math takes ~0.15 s per
frame. Filling, blitting and logic add roughly another 0.1 s. That gives an
**estimated 3–5 fps on a stock IBM PC/XT**, and more on a turbo XT with an
8087-2. These numbers are estimates and have not been measured on real
hardware. Measurements (86Box/PCem with an 8087, or a real machine) are welcome.

## Layout

```
src/flight.asm      the program
src/data.inc        generated tables + models (do not edit)
tools/gen_data.py   generator: sine table, dither ramps, models, face normals
build.sh            build script
FLIGHT87.COM        prebuilt binary
```
