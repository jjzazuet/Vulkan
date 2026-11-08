#!/usr/bin/env python3
"""Generate the fixed DOF fixture used by the minimal Rendu port.

Matches the Java DOF scene shader: 512x512, left half in focus at z=2,
right half at z=10, 32-cell checkerboard. The same raw files are used by the
Java `RzCppDofParityTest`.
"""

from pathlib import Path

import numpy as np

W = H = 512
Z_NEAR = 0.1
Z_FAR = 100.0
FOCUS_Z = 2.0
FAR_Z = 10.0
CHECKER_SCALE = 32.0
FOCUS_DIST = 2.0
FOCUS_SCALE = 10.0

ROOT = Path(__file__).resolve().parent


def depth_from_view_z(z: float) -> float:
    return Z_FAR * (z - Z_NEAR) / (z * (Z_FAR - Z_NEAR))


def main() -> None:
    yy, xx = np.mgrid[0:H, 0:W]
    uv_x = (xx + 0.5) / W
    uv_y = (yy + 0.5) / H
    in_focus = uv_x < 0.5

    cell_x = np.floor(uv_x * CHECKER_SCALE)
    cell_y = np.floor(uv_y * CHECKER_SCALE)
    checker = ((cell_x + cell_y) % 2.0).astype(np.float32)

    dark = np.where(in_focus[..., None],
                    np.array([0.12, 0.20, 0.34], dtype=np.float32),
                    np.array([0.10, 0.28, 0.46], dtype=np.float32))
    light = np.where(in_focus[..., None],
                     np.array([0.82, 0.70, 0.48], dtype=np.float32),
                     np.array([0.48, 0.82, 0.92], dtype=np.float32))
    color = dark + (light - dark) * checker[..., None]

    rgba = np.empty((H, W, 4), dtype=np.float16)
    rgba[..., :3] = color.astype(np.float16)
    rgba[..., 3] = 1.0
    rgba.tofile(ROOT / "color.rgba16f")

    depth = np.where(in_focus, depth_from_view_z(FOCUS_Z), depth_from_view_z(FAR_Z))
    depth.astype("<f4").tofile(ROOT / "depth.r32f")

    proj_x = -Z_FAR / (Z_FAR - Z_NEAR)
    proj_y = -Z_FAR * Z_NEAR / (Z_FAR - Z_NEAR)
    (ROOT / "params.txt").write_text(
        "\n".join(
            [
                f"width={W}",
                f"height={H}",
                f"zNear={Z_NEAR}",
                f"zFar={Z_FAR}",
                f"focusDist={FOCUS_DIST}",
                f"focusScale={FOCUS_SCALE}",
                f"projX={proj_x}",
                f"projY={proj_y}",
                "",
            ]
        )
    )
    print(f"wrote DOF fixture {W}x{H} to {ROOT}")


if __name__ == "__main__":
    main()
