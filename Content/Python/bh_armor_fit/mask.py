"""Body mask bake: which skin does a worn piece really enclose?

For every final armor piece a single-channel texture in the body mesh's UV0 space is written
(T_BHMask_<Set>_<Slot>_<Body>, 512 x 512, next to the final mesh). White = hide the skin there. The masked body
material (M_BH_Skin_Masked) fades the skin under the white areas; UBH_ArmorVisualComponent feeds the texture of
each worn piece into BH_Mask_<Slot>.

A body vertex is covered only when ALL of these hold (the mask must never hide arms, neck, face or anything the
piece leaves uncovered):
  * it is not weighted mostly to the head or neck bones (never head / face / neck, for any slot);
  * the garment surface is within MASK_REACH_CM of it, on the side its normal points to (cos >= MASK_ALIGN);
  * that garment point is at least MASK_EDGE_MARGIN_CM inside the garment's boundary loops (cuffs, neck hole,
    hems, sleeve ends), measured to boundary edges sampled every few mm;
  * after eroding the covered area by MASK_ERODE_RINGS vertex rings (so the visible skin edge never sits under a rim).
The garment point is the nearest point on the garment (an approximation of a ray along the normal; the alignment
test rejects garment parts on the other side of a gap, e.g. the other leg).

The pixel value is the covered flag interpolated over the UV triangles, so island borders fall at the middle of the
last covered / uncovered triangle (the material clips at 0.33 of 1 - mask, which errs towards showing skin).

Everything works in the bind pose, on the DynamicMesh the piece was just written from.
"""
import math
import os
import re
import struct
import time
import zlib

import unreal

from . import assets, clearance, config, weights
from . import report as rpt

_FIT = config.FIT
MASK_SIZE = int(_FIT["mask_size"])
MASK_REACH_CM = float(_FIT["mask_reach_cm"])
MASK_ALIGN = float(_FIT["mask_align"])
MASK_EDGE_MARGIN_CM = float(_FIT["mask_edge_margin_cm"])
MASK_ERODE_RINGS = int(_FIT["mask_erode_rings"])
PAD_PIXELS = 1
EDGE_SAMPLE_CM = 0.5
WELD_DECIMALS = 3  # positions equal to 0.001 cm are the same point (UV seams / split vertices)
_NEVER = re.compile(r"^(head|neck)")

SIGNATURE = assets.stamp_of(MASK_SIZE, MASK_REACH_CM, MASK_ALIGN, MASK_EDGE_MARGIN_CM, MASK_ERODE_RINGS, PAD_PIXELS, _NEVER.pattern, "v1")

_QUERIES = unreal.GeometryScript_MeshQueries
_SPATIAL = unreal.GeometryScript_MeshSpatial
_UVS = unreal.GeometryScript_UVs
_body_cache = {}


# ---------------------------------------------------------------------------
# small vector helpers on (x, y, z) tuples
# ---------------------------------------------------------------------------

def _xyz(v):
    return (v.x, v.y, v.z)


def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _length(a):
    return math.sqrt(_dot(a, a))


def _unit(a):
    n = _length(a)
    return (a[0] / n, a[1] / n, a[2] / n) if n > 1e-12 else (0.0, 0.0, 0.0)


def _weld_key(p):
    return tuple(round(c, WELD_DECIMALS) for c in p)


# ---------------------------------------------------------------------------
# mesh readers
# ---------------------------------------------------------------------------

def _positions(mesh):
    return {v: _xyz(clearance._position(mesh, v)) for v in clearance._vertex_ids(mesh)}


def _triangles(mesh):
    """[(triangle id, (a, b, c))] of every live triangle."""
    # Same call clearance._neighbours uses (proven). skip_gaps=False keeps list index == triangle id;
    # freed ids come back as degenerate (-1 or repeated) entries and are skipped.
    _, triangles, _gaps = _QUERIES.get_all_triangle_indices(mesh, False)
    result = []
    for tri, t in enumerate(triangles.convert_triangle_list_to_array()):
        if t.x < 0 or t.x == t.y or t.y == t.z or t.x == t.z:
            continue
        result.append((tri, (t.x, t.y, t.z)))
    return result


class _UVTri(object):
    def __init__(self, a, b, c):
        self.uv0, self.uv1, self.uv2 = a, b, c


def _uv_triangle(mesh, triangle_id):
    """UV0 corners of a triangle. MeshQueries.get_triangle_u_vs -> (uv1, uv2, uv3, valid)."""
    if hasattr(_QUERIES, "get_triangle_u_vs"):
        result = _QUERIES.get_triangle_u_vs(mesh, 0, triangle_id)
        uvs = [v for v in (result if isinstance(result, tuple) else (result,)) if isinstance(v, unreal.Vector2D)]
        if len(uvs) >= 3:
            return _UVTri(*uvs[:3])
    if hasattr(_UVS, "get_mesh_uv_triangle"):
        result = _UVS.get_mesh_uv_triangle(mesh, 0, triangle_id)
        for value in result if isinstance(result, tuple) else (result,):
            if hasattr(value, "uv0") and hasattr(value, "uv2"):
                return value
    raise RuntimeError("no UV triangle API worked (does the body mesh have UV0?)")


def _welded(positions):
    """({vertex: group id}, [group members]) with positions welded."""
    keys = {}
    group_of = {}
    members = []
    for vertex, p in positions.items():
        key = _weld_key(p)
        if key not in keys:
            keys[key] = len(members)
            members.append([])
        group_of[vertex] = keys[key]
        members[keys[key]].append(vertex)
    return group_of, members


# ---------------------------------------------------------------------------
# the body (cached per body mesh)
# ---------------------------------------------------------------------------

def _body_data(body_mesh_path):
    key = (body_mesh_path, assets.read_stamp(body_mesh_path), SIGNATURE)
    if key in _body_cache:
        return _body_cache[key]
    mesh, _ = clearance._body(body_mesh_path)
    positions = _positions(mesh)
    group_of, members = _welded(positions)
    triangles = _triangles(mesh)
    normals = [(0.0, 0.0, 0.0)] * len(members)
    neighbours = [set() for _ in members]
    for _, corners in triangles:
        a, b, c = (positions[v] for v in corners)
        face = _cross(_sub(b, a), _sub(c, a))  # area weighted
        ga, gb, gc = (group_of[v] for v in corners)
        for g in (ga, gb, gc):
            n = normals[g]
            normals[g] = (n[0] + face[0], n[1] + face[1], n[2] + face[2])
        neighbours[ga].update((gb, gc))
        neighbours[gb].update((ga, gc))
        neighbours[gc].update((ga, gb))
    names, _parents = weights.bone_table(mesh)
    bone_weights = weights._read_weights(mesh, list(positions))
    never = [False] * len(members)
    for vertex, w in bone_weights.items():
        if not w:
            continue
        dominant = max(w.items(), key=lambda item: item[1])[0]
        if _NEVER.match(names[dominant]):
            never[group_of[vertex]] = True
    uv_triangles = []
    for tri, corners in triangles:
        uv = _uv_triangle(mesh, tri)
        uv_triangles.append((
            [group_of[v] for v in corners],
            [(uv.uv0.x, uv.uv0.y), (uv.uv1.x, uv.uv1.y), (uv.uv2.x, uv.uv2.y)],
        ))
    data = {
        "positions": positions,
        "group_of": group_of,
        "members": members,
        "normals": [_unit(n) for n in normals],
        "neighbours": neighbours,
        "never": never,
        "uv_triangles": uv_triangles,
    }
    _body_cache.clear()
    _body_cache[key] = data
    return data


# ---------------------------------------------------------------------------
# garment boundary
# ---------------------------------------------------------------------------

def _boundary_grid(garment):
    """{cell: [boundary points]} of the garment's open edges (cuffs, hems, neck hole), sampled every EDGE_SAMPLE_CM."""
    positions = _positions(garment)
    group_of, _members = _welded(positions)
    count = {}
    for _, corners in _triangles(garment):
        g = [group_of[v] for v in corners]
        for a, b in ((g[0], g[1]), (g[1], g[2]), (g[2], g[0])):
            edge = (a, b) if a < b else (b, a)
            count[edge] = count.get(edge, 0) + 1
    point_of = {}
    for vertex, p in positions.items():
        point_of.setdefault(group_of[vertex], p)
    cell = MASK_EDGE_MARGIN_CM
    grid = {}
    boundary_edges = 0
    for (a, b), n in count.items():
        if n != 1:
            continue
        boundary_edges += 1
        pa, pb = point_of[a], point_of[b]
        length = _length(_sub(pb, pa))
        steps = max(1, int(length / EDGE_SAMPLE_CM))
        for i in range(steps + 1):
            t = i / float(steps)
            p = (pa[0] + (pb[0] - pa[0]) * t, pa[1] + (pb[1] - pa[1]) * t, pa[2] + (pb[2] - pa[2]) * t)
            grid.setdefault((int(p[0] // cell), int(p[1] // cell), int(p[2] // cell)), []).append(p)
    return grid, boundary_edges


def _boundary_distance_ok(grid, point):
    """True when no boundary sample lies within MASK_EDGE_MARGIN_CM of `point`."""
    cell = MASK_EDGE_MARGIN_CM
    cx, cy, cz = int(point[0] // cell), int(point[1] // cell), int(point[2] // cell)
    limit = MASK_EDGE_MARGIN_CM * MASK_EDGE_MARGIN_CM
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            for dz in (-1, 0, 1):
                for q in grid.get((cx + dx, cy + dy, cz + dz), ()):
                    d = _sub(q, point)
                    if _dot(d, d) < limit:
                        return False
    return True


# ---------------------------------------------------------------------------
# coverage, raster, texture
# ---------------------------------------------------------------------------

def coverage(garment, body_mesh_path):
    """Covered flag per welded body vertex group, plus a summary. Returns (flags, data, summary)."""
    data = _body_data(body_mesh_path)
    members, positions = data["members"], data["positions"]
    g_positions = _positions(garment)
    lows = [min(p[i] for p in g_positions.values()) - MASK_REACH_CM for i in range(3)]
    highs = [max(p[i] for p in g_positions.values()) + MASK_REACH_CM for i in range(3)]
    bvh = clearance._first(_SPATIAL.build_bvh_for_mesh(garment, None), unreal.GeometryScriptDynamicMeshBVH)
    grid, boundary_edges = _boundary_grid(garment)
    flags = [False] * len(members)
    tested = 0
    for group, vertices in enumerate(members):
        if data["never"][group]:
            continue
        p = positions[vertices[0]]
        if any(p[i] < lows[i] or p[i] > highs[i] for i in range(3)):
            continue
        tested += 1
        hit = clearance._first(
            _SPATIAL.find_nearest_point_on_mesh(garment, bvh, unreal.Vector(*p), clearance._OPTIONS),
            unreal.GeometryScriptTrianglePoint,
        )
        offset = _sub(_xyz(hit.position), p)
        distance = _length(offset)
        if distance > MASK_REACH_CM:
            continue
        if distance > 0.05 and _dot(offset, data["normals"][group]) < MASK_ALIGN * distance:
            continue
        if not _boundary_distance_ok(grid, _xyz(hit.position)):
            continue
        flags[group] = True
    covered = sum(flags)
    for _ in range(MASK_ERODE_RINGS):
        flags = [f and all(flags[n] for n in data["neighbours"][g]) for g, f in enumerate(flags)]
    summary = {
        "body_groups": len(members),
        "tested": tested,
        "covered_before_erosion": covered,
        "covered": sum(flags),
        "garment_boundary_edges": boundary_edges,
    }
    return flags, data, summary


def _rasterize(flags, data, size):
    pixels = bytearray(size * size)
    for groups, uvs in data["uv_triangles"]:
        values = [1.0 if flags[g] else 0.0 for g in groups]
        if not any(values):
            continue
        pts = [(u * size, v * size) for u, v in uvs]
        x0 = max(0, int(math.floor(min(p[0] for p in pts))))
        x1 = min(size - 1, int(math.ceil(max(p[0] for p in pts))))
        y0 = max(0, int(math.floor(min(p[1] for p in pts))))
        y1 = min(size - 1, int(math.ceil(max(p[1] for p in pts))))
        (ax, ay), (bx, by), (cx, cy) = pts
        denominator = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if abs(denominator) < 1e-9:
            continue
        for y in range(y0, y1 + 1):
            row = y * size
            for x in range(x0, x1 + 1):
                px, py = x + 0.5, y + 0.5
                w0 = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / denominator
                w1 = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / denominator
                w2 = 1.0 - w0 - w1
                if w0 < -0.02 or w1 < -0.02 or w2 < -0.02:
                    continue
                value = int(255.0 * (w0 * values[0] + w1 * values[1] + w2 * values[2]) + 0.5)
                if value > pixels[row + x]:
                    pixels[row + x] = min(255, value)
    for _ in range(PAD_PIXELS):  # 3 x 3 maximum filter: keeps bilinear sampling at island borders from bleeding in black
        source = bytes(pixels)
        for y in range(size):
            for x in range(size):
                if source[y * size + x]:
                    continue
                best = 0
                for ny in (max(0, y - 1), y, min(size - 1, y + 1)):
                    for nx in (max(0, x - 1), x, min(size - 1, x + 1)):
                        best = max(best, source[ny * size + nx])
                pixels[y * size + x] = best
    return pixels


def _write_png(path, size, pixels):
    raw = b"".join(b"\x00" + bytes(pixels[y * size:(y + 1) * size]) for y in range(size))

    def chunk(kind, payload):
        body = kind + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 0, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as handle:
        handle.write(png)


def _import_texture(package, pixels, size):
    """Import the pixels as a Texture2D at `package` (replaces an existing one) with mask settings."""
    folder, name = package.rsplit("/", 1)
    assets.ensure_folder(folder)
    temp_dir = os.path.join(unreal.SystemLibrary.get_project_saved_directory(), "ArmorFit", "Masks")
    os.makedirs(temp_dir, exist_ok=True)
    png = os.path.join(temp_dir, name + ".png")
    _write_png(png, size, pixels)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", png)
    task.set_editor_property("destination_path", folder)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("replace_existing_settings", True)
    task.set_editor_property("save", False)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = assets.load(package)
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    texture.set_editor_property("srgb", False)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
    texture.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
    texture.modify(True)
    texture.post_edit_change()
    return texture


def bake(piece, body, garment, stamp, label=""):
    """Bake and import the mask of `piece` (final DynamicMesh `garment`); returns (texture package, summary, flags, data).

    Returns ('', {...}, None, None) for pieces that get no mask.
    """
    package = piece.mask_path(body)
    if not package:
        return "", {"mask": "none for this piece"}, None, None
    started = time.time()
    flags, data, summary = coverage(garment, body.body_mesh_path)
    pixels = _rasterize(flags, data, MASK_SIZE)
    summary["white_pixels"] = sum(1 for p in pixels if p >= 170)
    texture = _import_texture(package, pixels, MASK_SIZE)
    assets.write_stamp(texture, stamp)
    assets.save([package])
    rpt.log(config.LOG_FIT, "%s mask %s -> %s (%.1f s)" % (label, summary, package, time.time() - started))
    return package, summary, flags, data
