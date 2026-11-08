#!/usr/bin/env python3
"""Generate the fixed SSAO fixture: a rasterized Utah teapot.

The same raw files are vendored in zt-gfx for the Java parity test, so both
renderers consume byte-identical inputs. The teapot OBJ is from
`alecjacobson/common-3d-test-models`; the Utah teapot is a public-domain model.
"""

import math
from pathlib import Path

import numpy as np

W = H = 512
Z_NEAR = 0.1
Z_FAR = 100.0
BACKGROUND_Z = 8.0

ROOT = Path(__file__).resolve().parent
OBJ = ROOT / "teapot.obj"


def load_obj(path: Path):
    verts = []
    faces = []
    for line in path.read_text().splitlines():
        if line.startswith("v "):
            parts = line.split()
            verts.append([float(parts[1]), float(parts[2]), float(parts[3])])
        elif line.startswith("f "):
            parts = line.split()[1:]
            idx = [int(p.split("/")[0]) - 1 for p in parts]
            for i in range(1, len(idx) - 1):
                faces.append([idx[0], idx[i], idx[i + 1]])
    return np.asarray(verts, dtype=np.float64), np.asarray(faces, dtype=np.int32)


def depth_from_view_z(z: float) -> float:
    return Z_FAR * (z - Z_NEAR) / (z * (Z_FAR - Z_NEAR))


def look_at(eye, target, up):
    eye = np.asarray(eye, dtype=np.float64)
    target = np.asarray(target, dtype=np.float64)
    up = np.asarray(up, dtype=np.float64)
    f = target - eye
    f /= np.linalg.norm(f)
    s = np.cross(f, up)
    s /= np.linalg.norm(s)
    u = np.cross(s, f)
    m = np.eye(4)
    m[0, :3] = s
    m[1, :3] = u
    m[2, :3] = -f
    m[0, 3] = -np.dot(s, eye)
    m[1, 3] = -np.dot(u, eye)
    m[2, 3] = np.dot(f, eye)
    return m


def perspective(fovy_deg, aspect, near, far):
    f = 1.0 / math.tan(math.radians(fovy_deg) * 0.5)
    m = np.zeros((4, 4))
    m[0, 0] = f / aspect
    m[1, 1] = f
    m[2, 2] = (far + near) / (near - far)
    m[2, 3] = (2.0 * far * near) / (near - far)
    m[3, 2] = -1.0
    return m


def main() -> None:
    verts, faces = load_obj(OBJ)
    center = (verts.min(axis=0) + verts.max(axis=0)) * 0.5
    verts = verts - center
    extent = float(np.max(verts.max(axis=0) - verts.min(axis=0)))
    verts *= 3.0 / extent

    # Ground plane for contact AO.
    base_y = float(verts[:, 1].min()) - 0.02
    plane_offset = len(verts)
    verts = np.vstack([verts, np.array([
        [-5.0, base_y, -5.0],
        [ 5.0, base_y, -5.0],
        [ 5.0, base_y,  5.0],
        [-5.0, base_y,  5.0],
    ])])
    plane_faces = np.array([
        [plane_offset, plane_offset + 1, plane_offset + 2],
        [plane_offset, plane_offset + 2, plane_offset + 3],
    ], dtype=np.int32)
    faces = np.vstack([faces, plane_faces])
    face_base = [np.array([0.82, 0.62, 0.40])] * (len(faces) - len(plane_faces))
    face_base += [np.array([0.32, 0.34, 0.38])] * len(plane_faces)

    eye = np.array([3.6, 2.2, 4.9])
    view = look_at(eye, np.array([0.0, 0.0, 0.0]), np.array([0.0, 1.0, 0.0]))
    proj = perspective(45.0, 1.0, Z_NEAR, Z_FAR)

    depth = np.full((H, W), depth_from_view_z(BACKGROUND_Z), dtype=np.float32)
    yy, xx = np.mgrid[0:H, 0:W]
    checker = ((xx // 32 + yy // 32) & 1).astype(np.float64)
    color = np.zeros((H, W, 4), dtype=np.uint8)
    color[..., 0] = (72 + 20 * checker).astype(np.uint8)
    color[..., 1] = (82 + 20 * checker).astype(np.uint8)
    color[..., 2] = (104 + 24 * checker).astype(np.uint8)
    color[..., 3] = 255

    light = np.array([0.45, 0.8, 0.35])
    light /= np.linalg.norm(light)

    for fi, face in enumerate(faces):
        world = verts[face]
        norm = np.cross(world[1] - world[0], world[2] - world[0])
        nlen = np.linalg.norm(norm)
        if nlen < 1e-12:
            continue
        norm /= nlen
        view_dir = world.mean(axis=0) - eye
        if np.dot(norm, view_dir) > 0:
            norm = -norm

        clip = np.empty((3, 4))
        for i, p in enumerate(world):
            clip[i] = proj @ (view @ np.array([p[0], p[1], p[2], 1.0]))

        w = clip[:, 3]
        if np.any(w <= 1e-6):
            continue

        ndc = clip[:, :2] / w[:, None]
        sx = (ndc[:, 0] * 0.5 + 0.5) * (W - 1)
        sy = (-ndc[:, 1] * 0.5 + 0.5) * (H - 1)

        min_x = max(0, int(math.floor(sx.min())))
        max_x = min(W - 1, int(math.ceil(sx.max())))
        min_y = max(0, int(math.floor(sy.min())))
        max_y = min(H - 1, int(math.ceil(sy.max())))
        if min_x > max_x or min_y > max_y:
            continue

        area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sy[1] - sy[0]) * (sx[2] - sx[0])
        if abs(area) < 1e-12:
            continue
        inv_area = 1.0 / area

        diffuse = max(float(np.dot(norm, light)), 0.0)
        shade = np.clip(0.22 + 0.78 * diffuse, 0.0, 1.0)
        rgb = np.clip(face_base[fi] * shade * 255.0, 0, 255).astype(np.uint8)

        inv_w = 1.0 / w
        for py in range(min_y, max_y + 1):
            for px in range(min_x, max_x + 1):
                x = px + 0.5
                y = py + 0.5
                w0 = ((sx[1] - x) * (sy[2] - y) - (sy[1] - y) * (sx[2] - x)) * inv_area
                w1 = ((sx[2] - x) * (sy[0] - y) - (sy[2] - y) * (sx[0] - x)) * inv_area
                w2 = 1.0 - w0 - w1
                if w0 < -1e-6 or w1 < -1e-6 or w2 < -1e-6:
                    continue
                inv = w0 * inv_w[0] + w1 * inv_w[1] + w2 * inv_w[2]
                if inv <= 0.0:
                    continue
                d = 1.0 / inv
                z = depth_from_view_z(d)
                if z < depth[py, px]:
                    depth[py, px] = z
                    color[py, px, :3] = rgb

    depth.astype("<f4").tofile(ROOT / "depth.r32f")
    color.tofile(ROOT / "color.rgba8")
    (ROOT / "params.txt").write_text(
        "\n".join(
            [
                f"width={W}",
                f"height={H}",
                f"zNear={Z_NEAR}",
                f"zFar={Z_FAR}",
                "radius=1.0",
                "attScale=1.0",
                "distScale=10.0",
                "blurDepthThreshold=30.0",
                "combineScale=2.0",
                "combineBias=0.0",
                "",
            ]
        )
    )
    print(f"wrote teapot {W}x{H} fixture to {ROOT}")


if __name__ == "__main__":
    main()
