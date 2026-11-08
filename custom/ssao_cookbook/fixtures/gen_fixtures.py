#!/usr/bin/env python3
"""Generate the fixed SSAO fixture used by the minimal cookbook port.

The same raw files are vendored in zt-gfx for the Java parity test, so both
renderers consume byte-identical inputs. Run from this directory.
"""

import struct
from pathlib import Path

W = H = 512
Z_NEAR = 0.1
Z_FAR = 100.0
SPHERE_RADIUS = 0.45
SPHERE_Z = 3.0
BACKDROP_Z = 8.0


def depth_from_view_z(z: float) -> float:
    # Inverse of the cookbook scaleZ() for Vulkan [0, 1] depth.
    return Z_FAR * (z - Z_NEAR) / (z * (Z_FAR - Z_NEAR))


def main() -> None:
    out = Path(__file__).resolve().parent
    depth = bytearray()
    color = bytearray()

    for y in range(H):
        ny = (y + 0.5) / H * 2.0 - 1.0
        for x in range(W):
            nx = (x + 0.5) / W * 2.0 - 1.0
            thin_bar = (x % 32 == 0) or (y % 32 == 0)
            if thin_bar:
                z = SPHERE_Z
                rgb = (240, 220, 80)
            elif nx * nx + ny * ny < SPHERE_RADIUS * SPHERE_RADIUS:
                z = SPHERE_Z
                rgb = (220, 120, 80)
            else:
                z = BACKDROP_Z
                checker = ((x // 32) + (y // 32)) & 1
                rgb = (60, 80, 110) if checker else (120, 140, 170)
            depth += struct.pack("<f", depth_from_view_z(z))
            color += bytes((rgb[0], rgb[1], rgb[2], 255))

    rotation = bytearray()
    for i in range(16):
        rotation += bytes(
            (
                (i * 73 + 41) & 0xFF,
                (i * 151 + 17) & 0xFF,
                (i * 199 + 89) & 0xFF,
                255,
            )
        )

    (out / "depth.r32f").write_bytes(depth)
    (out / "color.rgba8").write_bytes(color)
    (out / "rotation.rgba8").write_bytes(rotation)
    (out / "params.txt").write_text(
        "\n".join(
            [
                f"width={W}",
                f"height={H}",
                f"zNear={Z_NEAR}",
                f"zFar={Z_FAR}",
                "radius=0.10",
                "attScale=1.0",
                "distScale=10.0",
                "blurDepthThreshold=30.0",
                "combineScale=1.5",
                "combineBias=0.16",
                "",
            ]
        )
    )
    print(f"wrote {W}x{H} depth/color, 4x4 rotation to {out}")


if __name__ == "__main__":
    main()
