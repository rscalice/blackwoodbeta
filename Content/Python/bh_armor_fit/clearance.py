"""Body clearance: push garment vertices that sit inside (or touching) the body skin out to a small gap.

Many packs are authored skin-tight on the assumption that the body underneath is hidden (the spell-caster
set sits ~2.5 mm *inside* its authoring body). Resizing keeps that offset, so after the resize the body still
pokes through. This pass measures each garment vertex against the body type's mesh in bind pose and, where
the vertex is closer than CLEARANCE_CM to the skin (or inside it), moves it out along the skin normal.
Loose areas are left untouched. The displacement is feathered over neighbouring vertices so the push does not
leave ridges, and it never shrinks a push below what the vertex itself needs.
"""
import time

import unreal

from . import assets, config
from . import report as rpt

CLEARANCE_CM = 0.6        # wanted gap between skin and garment
MAX_DEPTH_CM = 4.0        # vertices deeper than this inside the body are deliberate (tucked edges): left alone
MAX_REACH_CM = 4.0        # vertices farther than this from the skin are not touched
FEATHER_PASSES = 2

SIGNATURE = assets.stamp_of(CLEARANCE_CM, MAX_DEPTH_CM, MAX_REACH_CM, FEATHER_PASSES)

_SPATIAL = unreal.GeometryScript_MeshSpatial
_QUERIES = unreal.GeometryScript_MeshQueries
_EDIT = unreal.GeometryScript_MeshEdits
_OPTIONS = unreal.GeometryScriptSpatialQueryOptions()

_body_cache = {}


def _first(result, kind):
    for value in result if isinstance(result, tuple) else (result,):
        if isinstance(value, kind):
            return value
    raise RuntimeError("GeometryScript call returned no %s" % kind.__name__)


def _body(body_mesh_path):
    """(DynamicMesh, BVH) of a body mesh, cached per path and stamp."""
    key = (body_mesh_path, assets.read_stamp(body_mesh_path))
    if key not in _body_cache:
        from . import meshes
        mesh = meshes.read(assets.load(body_mesh_path))
        bvh = _first(_SPATIAL.build_bvh_for_mesh(mesh, None), unreal.GeometryScriptDynamicMeshBVH)
        _body_cache.clear()
        _body_cache[key] = (mesh, bvh)
    return _body_cache[key]


def _vertex_ids(mesh):
    _, ids, _gaps = _QUERIES.get_all_vertex_i_ds(mesh)
    return list(ids.convert_index_list_to_array())


def _position(mesh, vertex):
    return _first(_QUERIES.get_vertex_position(mesh, vertex), unreal.Vector)


def measure(mesh, body_mesh_path):
    """{vertex: (signed distance to skin in cm, + = outside; skin normal)} for vertices within reach."""
    body, bvh = _body(body_mesh_path)
    result = {}
    for vertex in _vertex_ids(mesh):
        position = _position(mesh, vertex)
        hit = _first(_SPATIAL.find_nearest_point_on_mesh(body, bvh, position, _OPTIONS), unreal.GeometryScriptTrianglePoint)
        offset = position - hit.position
        if offset.length() > MAX_REACH_CM:
            continue
        normal = _first(_QUERIES.get_triangle_face_normal(body, hit.triangle_id), unreal.Vector)
        result[vertex] = (offset.dot(normal), normal)
    return result


def _neighbours(mesh):
    _, triangles, _gaps = _QUERIES.get_all_triangle_indices(mesh, True)
    adjacent = {}
    for tri in triangles.convert_triangle_list_to_array():
        for a, b in ((tri.x, tri.y), (tri.y, tri.z), (tri.z, tri.x)):
            adjacent.setdefault(a, set()).add(b)
            adjacent.setdefault(b, set()).add(a)
    return adjacent


def push_out(mesh, body_mesh_path, label=""):
    """Move garment vertices out to CLEARANCE_CM from the body skin; returns (mesh, summary)."""
    started = time.time()
    measured = measure(mesh, body_mesh_path)
    push = {}
    for vertex, (distance, normal) in measured.items():
        if -MAX_DEPTH_CM < distance < CLEARANCE_CM:
            push[vertex] = normal * (CLEARANCE_CM - distance)
    inside = sum(1 for d, _ in measured.values() if -MAX_DEPTH_CM < d < 0.0)
    if push and FEATHER_PASSES:
        adjacent = _neighbours(mesh)
        for _ in range(FEATHER_PASSES):
            feathered = dict(push)
            for vertex in set(push) | {n for v in push for n in adjacent.get(v, ())}:
                ring = [vertex] + list(adjacent.get(vertex, ()))
                total = unreal.Vector(0.0, 0.0, 0.0)
                for other in ring:
                    total = total + push.get(other, unreal.Vector(0.0, 0.0, 0.0))
                average = total * (1.0 / len(ring))
                own = push.get(vertex)
                feathered[vertex] = own if own is not None and own.length() >= average.length() else average
            push = feathered
    moved = 0
    largest = 0.0
    for vertex, delta in push.items():
        if delta.length() < 1e-4:
            continue
        mesh, _ = _EDIT.set_vertex_position(mesh, vertex, _position(mesh, vertex) + delta, True)
        moved += 1
        largest = max(largest, delta.length())
    summary = {"near_skin": len(measured), "inside": inside, "moved": moved, "max_push_cm": round(largest, 2)}
    rpt.log(config.LOG_FIT, "%s clearance %s (%.1f s)" % (label, summary, time.time() - started))
    return mesh, summary


def poke_ratio(mesh, body_mesh_path):
    """Share of measured garment vertices that are inside the skin (diagnostic)."""
    measured = measure(mesh, body_mesh_path)
    if not measured:
        return 0.0
    return sum(1 for d, _ in measured.values() if d < 0.0) / float(len(measured))
