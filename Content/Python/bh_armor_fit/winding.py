"""Winding check (report only): how many garment triangles face INTO the body?

A triangle within NEAR_CM of the skin counts as inverted when its face normal points toward the nearest body point
(dot(normal, centroid - nearest point) < 0). Mirrored geometry with flipped winding shows up as a high percentage on
one side (x < 0 or x > 0) only. Logged as [BH_ArmorFit] winding.
"""
import unreal

from . import clearance, config
from . import report as rpt

NEAR_CM = 5.0
MAX_TRIANGLES = 20000
_QUERIES = unreal.GeometryScript_MeshQueries


def check(mesh, body_mesh_path, label):
    """Returns a short text; logs one line per side. Never raises."""
    try:
        body, bvh = clearance._body(body_mesh_path)
        _, triangles, _gaps = _QUERIES.get_all_triangle_indices(mesh, True)
        triangles = list(triangles.convert_triangle_list_to_array())
        stride = max(1, len(triangles) // MAX_TRIANGLES)
        positions = {}

        def pos(v):
            if v not in positions:
                positions[v] = clearance._position(mesh, v)
            return positions[v]

        counts = {"x>0": [0, 0], "x<0": [0, 0]}  # [near triangles, inverted]
        for tri in triangles[::stride]:
            a, b, c = pos(tri.x), pos(tri.y), pos(tri.z)
            normal = (b - a).cross(c - a)
            if normal.length() < 1e-9:
                continue
            centre = (a + b + c) * (1.0 / 3.0)
            hit = clearance._first(
                clearance._SPATIAL.find_nearest_point_on_mesh(body, bvh, centre, clearance._OPTIONS),
                unreal.GeometryScriptTrianglePoint,
            )
            away = centre - hit.position
            if away.length() > NEAR_CM:
                continue
            row = counts["x>0" if centre.x > 0 else "x<0"]
            row[0] += 1
            if normal.dot(away) < 0.0:
                row[1] += 1
        parts = []
        for side, (near, bad) in counts.items():
            percent = 100.0 * bad / near if near else 0.0
            parts.append("%s %.0f%% of %d" % (side, percent, near))
            (rpt.warn if percent > 30.0 and near > 50 else rpt.log)(
                config.LOG_FIT, "%s winding %s: %d / %d near-skin triangles face into the body (%.0f%%)" % (label, side, bad, near, percent))
        return "; winding inverted " + ", ".join(parts)
    except Exception as exc:  # noqa: BLE001 - report-only
        rpt.warn(config.LOG_FIT, "%s winding check failed: %s" % (label, exc))
        return "; winding check failed"