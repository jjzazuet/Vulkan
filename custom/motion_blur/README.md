# zt-gfx camera motion blur — minimal baseline

This is a standalone baseline for the camera-only motion blur merged into
`zt-gfx` as B6 (issue #180). It is **not** an external reference capture: no
external example with the identical fixed-tap, camera-only algorithm was
found. The owner approved this same-algorithm baseline.

The shaders are the exact files used by the Java fixture:

- `shaders/scene.frag` — screen-space line, constant depth, camera reprojection
  velocity
- `shaders/motion.frag` — 8 taps per side, symmetric along the velocity buffer,
  scaled by `shutterAngle / 360`, clamped to `maxBlurPixels`
- `shaders/fullscreen.vert` — fullscreen triangle

## Fixture

- 256x256
- camera position `(0, 0, 5)`, yfov `1.0` rad, aspect `1`, near `0.1`,
  far `100`
- frame 1 at yaw `0`, frame 2 at yaw `8` degrees
- reprojection is `previousViewProj * inverse(currentViewProj)` for that pair,
  dumped from the Java matrix utilities with exact IEEE-754 bits
- `shutterAngle = 180`, `maxBlurPixels = 16`
- color target `RGBA16F`, velocity target `RG16F`, depth `D32_SFLOAT`

## Build

```bash
cmake -S . -B build
cmake --build build --target motion_blur -j"$(nproc)"
```

## Run

```bash
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
  ./bin/motion_blur motion.ppm
```

Two consecutive runs are byte-identical:

```text
7ba95198d81049a3283b9f3ce4a1fff62e62f8e93750f198aadf92163a9367e9
```

`screenshot.png` is the verified 256x256 result. It is pixel-identical to the
Java `RzVkMotionBlurTest` final capture for the same scene (`0` differing
pixels, exact 8-bit comparison). The golden and `RzCppMotionBlurParityTest`
live in `zt-gfx`.
