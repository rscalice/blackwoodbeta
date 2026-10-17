"""Body clearance: push garment vertices that sit inside (or touching) the body skin out to a small gap.

Many packs are authored skin-tight on the assumption that the body underneath is hidden (the spell-caster
set sits ~2.5 mm *inside* its authoring body). Resizing keeps that offset, so after the resize the body still
pokes through. This pass measures each garment vertex against the body type's mesh in bind pose and, where
the vertex is closer than CLEARANCE_CM to the skin (or inside it), moves it out along the skin normal.
Loose areas are left untouched. The displacement is feathered over neighbouring vertices so the push does not
leave ridges, and it never shrinks a push below what the vertex itself needs.
"""
import re
import time

import unreal

from . import assets, config
from . import report as rpt

_FIT = config.FIT
CLEARANCE_CM = float(_FIT["clearance_cm"])             # wanted gap between skin and garment (1.0 cm)
JOINT_CLEARANCE_CM = float(_FIT["joint_clearance_cm"])  # ... in joint bands (1.5 cm)
JOINT_GAIN = float(_FIT["joint_gain"])
MAX_DEPTH_CM = 4.0        # vertices deeper than this inside the body are deliberate (tucked edges): left alone
MAX_REACH_CM = 4.0        # vertices farther than this from the skin are not touched (must exceed the largest clearance)
FEATHER_PASSES = 2

# Joint bands, chosen by bone weight: a vertex is in a joint band when it is weighted to BOTH bone families of the joint
# (50 / 50 = full band). Families are matched on the bone name prefix, so twist and corrective bones count with their bone.
_FAMILIES = (
    ("pelvis", re.compile(r"^pelvis")),
    ("spine", re.compile(r"^spine_")),
    ("clavicle", re.compile(r"^clavicle")),
    ("upperarm", re.compile(r"^upperarm")),
    ("lowerarm", re.compile(r"^lowerarm")),
    ("hand", re.compile(r"^(hand_|thumb_|index_|middle_|ring_|pinky_)")),
    ("thigh", re.compile(r"^thigh")),
    ("calf", re.compile(r"^calf")),
)
JOINT_BANDS = {
    "shoulder": ({"clavicle", "spine"}, {"upperarm"}),
    "elbow": ({"upperarm"}, {"lowerarm"}),
    "wrist": ({"lowerarm"}, {"hand"}),
    "hip": ({"pelvis"}, {"thigh"}),
    "knee": ({"thigh"}, {"calf"}),
}

SIGNATURE = assets.stamp_of(
    CLEARANCE_CM, JOINT_CLEARANCE_CM, JOINT_GAIN, MAX_DEPTH_CM, MAX_REACH_CM, FEATHER_PASSES,
    sorted((k, sorted(a), sorted(b)) for k, (a, b) in JOINT_BANDS.items()), "v2",
)

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


def _family(bone_name):
    for family, pattern in _FAMILIES:
        if pattern.match(bone_name):
            return family
    return ""


def joint_factors(mesh):
    """{vertex: 0..1} joint-band factor of every garment vertex that is weighted to both families of a joint."""
    from . import weights
    names, _ = weights.bone_table(mesh)
    families = [_family(n) for n in names]
    result = {}
    for vertex, bone_weights in weights._read_weights(mesh, _vertex_ids(mesh)).items():
        sums = {}
        for bone, weight in bone_weights.items():
            sums[families[bone]] = sums.get(families[bone], 0.0) + weight
        factor = 0.0
        for side_a, side_b in JOINT_BANDS.values():
            both = min(sum(sums.get(f, 0.0) for f in side_a), sum(sums.get(f, 0.0) for f in side_b))
            factor = max(factor, min(1.0, JOINT_GAIN * both))
        if factor > 0.0:
            result[vertex] = factor
    return result


def push_out(mesh, body_mesh_path, label=""):
    """Move garment vertices out to CLEARANCE_CM (JOINT_CLEARANCE_CM in joint bands) from the body skin; returns (mesh, summary)."""
    started = time.time()
    measured = measure(mesh, body_mesh_path)
    joints = joint_factors(mesh)
    push = {}
    for vertex, (distance, normal) in measured.items():
        wanted = CLEARANCE_CM + (JOINT_CLEARANCE_CM - CLEARANCE_CM) * joints.get(vertex, 0.0)
        if -MAX_DEPTH_CM < distance < wanted:
            push[vertex] = normal * (wanted - distance)
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
    summary = {"near_skin": len(measured), "inside": inside, "in_joint_band": sum(1 for v in measured if v in joints), "moved": moved, "max_push_cm": round(largest, 2)}
    rpt.log(config.LOG_FIT, "%s clearance %s (%.1f s)" % (label, summary, time.time() - started))
    return mesh, summary


def poke_ratio(mesh, body_mesh_path):
    """Share of measured garment vertices that are inside the skin (diagnostic)."""
    measured = measure(mesh, body_mesh_path)
    if not measured:
        return 0.0
    return sum(1 for d, _ in measured.values() if d < 0.0) / float(len(measured))
