# McGuire motion-blur reconstruction — camera-only fixture

Fragment-only port of the McGuire/Guertin reconstruction filter used by the
zt-gfx motion-blur spike (#291). Per-object velocity is deliberately out of
scope: `scene.frag` emits camera-only velocity for a fixed yaw 0 -> 8 degree
pair.

```text
scene.frag        256x256  color RGBA16F + velocity RG16F + depth D32F
tile_max_x.frag    16x256  R8G8_UNORM  max over each 16-pixel horizontal span
tile_max_y.frag    16x 16  R8G8_UNORM  max over each 16-pixel vertical span
neighbor_max.frag  16x 16  R8G8_UNORM  3x3 dilation
reconstruct.frag  256x256  RGBA16F     15 jittered, depth-weighted taps
```

## Fixture

- 256x256, tile size 16, 15 sample taps, shutter 180 degrees, max blur 16 px
- camera position `(0, 0, 5)`, yfov `1.0` rad, aspect `1`, near `0.1`, far `100`
- previous/current view-projection pair at yaw `0` / `8` degrees
- tile and neighbor velocities are `velocityPx / maxBlurPixels`, encoded into
  `R8G8_UNORM` with `encoded = value * 0.5 + 0.5`
- reconstruction linearizes depth with the same near/far pair and a
  `SOFT_Z_EXTENT = 0.02` view-unit constant as the Java shaders
- all sampling uses `texelFetch` with integer coordinates

## Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target motion_blur_mcguire -j"$(nproc)"
```

## Run

```bash
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
  ./bin/motion_blur_mcguire motion.ppm
```

Besides the PPM, the harness dumps the blur inputs next to it:
`<output>.color.rgba16f`, `<output>.depth.r32f` and
`<output>.velocity.rg16f`. These are byte-identical to the retired B6 capture,
so the Java parity test consumes exactly the same color/depth/velocity data.

Two consecutive runs are byte-identical:

```text
44ce5f630f2eda8cd65e9dc6629dc91c424f8487d647afc443e8cab946fe2242  motion.ppm
```

`screenshot.png` is the verified 256x256 result. `RzCppMotionBlurParityTest`
and the vendored golden live in `zt-gfx`.

## Algorithm notes

- Velocity is clamped to `min(tileSize, maxBlurPixels)` pixels, as in the
  reference addon.
- The tile maximum is accumulated on the raw velocity before the clamped
  vector is stored, so the selected direction and magnitude match the
  reference.
- Reconstruction weights follow the reference: foreground, background,
  cylinder, and the 2x simultaneous-blur term, plus the center tap weighted by
  `1 / clampedVelocityLength`.
- The reference's per-pixel jitter is reproduced with the same hash so the two
  sides stay deterministic.

Source algorithm: `wojtekpil/godot_per_object_motion_blur` @
`8fdc341062fb9592ea92402e4f3e1951d374204a`, MIT.
