"""Skin-weight cleanup for armor meshes.

MetaHuman bodies carry hundreds of helper and corrective bones (thigh_twistCor_*, calf_bck_*, finger
bulge/half/side bones ...). At runtime the retarget and post-process graphs move these helpers
several centimetres away from their rest position, so armor whose weights sit on them tears. Final armor
is therefore skinned to the CORE bones only: weight on any other bone is moved to its nearest core
ancestor, lightly smoothed over the mesh surface, and limited to MAX_INFLUENCES per vertex.
"""
import re
import time

import unreal

from . import assets, config
from . import report as rpt

# Bones armor may be skinned to. All of them exist on the canonical skeleton and stay on their rest
# local position at runtime, except pelvis, which the animation itself moves and every mesh follows.
CORE_BONE_PATTERN = re.compile(
    r"^(root|pelvis|spine_0[1-5]|neck_0[12]|head|clavicle_[lr]|upperarm_[lr]|lowerarm_[lr]|hand_[lr]"
    r"|(thumb|index|middle|ring|pinky)_(metacarpal|0[1-3])_[lr]|thigh_[lr]|calf_[lr]|foot_[lr]|ball_[lr]"
    r"|(upperarm|lowerarm|thigh|calf)_twist_0[12]_[lr])$"
)
MAX_INFLUENCES = 4
# One Laplacian pass: each vertex keeps (1 - SMOOTH_STRENGTH) of its own weights and takes
# SMOOTH_STRENGTH of its neighbours' average.
SMOOTH_STRENGTH = 0.25
SMOOTH_PASSES = 1
_UNCHANGED_TOLERANCE = 1e-4

# Part of every output stamp: changing the rules above makes existing outputs stale.
SIGNATURE = assets.stamp_of(CORE_BONE_PATTERN.pattern, MAX_INFLUENCES, SMOOTH_STRENGTH, SMOOTH_PASSES)

# Near-skin transfer (Armor Refit 2, used by harvest only; rigging keeps core-only weights): garment vertices close to the
# body skin take the nearest body point's weights over ALL bones (twist / corrective included; Leader Pose copies those bones
# from the body, so the garment deforms with the skin), fading back to the cleaned core weights further out. Bone NAMES are
# unchanged, so the canonical skeleton still matches. Part of TRANSFER_SIGNATURE (harvest stamps), not of SIGNATURE (rig stamps).
_FIT = config.FIT
TRANSFER_ENABLED = bool(_FIT["transfer"])
TRANSFER_FULL_CM = float(_FIT["transfer_full_cm"])
TRANSFER_FADE_CM = float(_FIT["transfer_fade_cm"])
TRANSFER_SMOOTH_RINGS = int(_FIT["transfer_smooth_rings"])
TRANSFER_KEEP = str(_FIT.get("transfer_keep", "all"))
TRANSFER_SIGNATURE = assets.stamp_of(TRANSFER_ENABLED, TRANSFER_FULL_CM, TRANSFER_FADE_CM, TRANSFER_SMOOTH_RINGS, TRANSFER_KEEP, "v2")

_WEIGHTS = unreal.GeometryScript_BoneWeights
_QUERIES = unreal.GeometryScript_MeshQueries

_body_weight_cache = {}


def is_core(bone_name):
    return CORE_BONE_PATTERN.match(str(bone_name)) is not None


def bone_table(mesh):
    """([bone name], [parent index]) of the skeleton carried by a DynamicMesh, in bone-index order."""
    _, infos = _WEIGHTS.get_all_bones_info(mesh)
    infos = sorted(infos, key=lambda info: info.index)
    return [str(i.name) for i in infos], [i.parent_index for i in infos]


def core_targets(names, parents):
    """For every bone index, the index of the bone itself when it is core, else of its nearest core ancestor."""
    targets = []
    for index, name in enumerate(names):
        if is_core(name):
            targets.append(index)
        elif parents[index] < 0:
            raise RuntimeError("bone '%s' has no core ancestor" % name)
        else:
            targets.append(targets[parents[index]])  # parents precede their children in a reference skeleton
    return targets


def _vertex_ids(mesh):
    _, ids, _gaps = _QUERIES.get_all_vertex_i_ds(mesh)
    return list(ids.convert_index_list_to_array())


def _read_weights(mesh, vertex_ids):
    """{vertex: {bone index: weight}} for every vertex; padding entries (weight 0) are dropped."""
    result = {}
    for vertex in vertex_ids:
        _, entries, _valid = _WEIGHTS.get_vertex_bone_weights(mesh, vertex)
        weights = {}
        for entry in entries:
            if entry.weight > 0.0:
                weights[entry.bone_index] = weights.get(entry.bone_index, 0.0) + entry.weight
        result[vertex] = weights
    return result


def _neighbours(mesh):
    """{vertex: set of vertices sharing a triangle edge}."""
    _, triangles, _gaps = _QUERIES.get_all_triangle_indices(mesh, True)
    adjacent = {}
    for tri in triangles.convert_triangle_list_to_array():
        for a, b in ((tri.x, tri.y), (tri.y, tri.z), (tri.z, tri.x)):
            adjacent.setdefault(a, set()).add(b)
            adjacent.setdefault(b, set()).add(a)
    return adjacent


def _remap(weights, targets):
    remapped = {}
    for bone, weight in weights.items():
        target = targets[bone]
        remapped[target] = remapped.get(target, 0.0) + weight
    return remapped


def _smooth(weights, adjacent):
    smoothed = {}
    for vertex, own in weights.items():
        near = adjacent.get(vertex)
        if not near:
            smoothed[vertex] = own
            continue
        blended = {bone: weight * (1.0 - SMOOTH_STRENGTH) for bone, weight in own.items()}
        share = SMOOTH_STRENGTH / len(near)
        for other in near:
            for bone, weight in weights[other].items():
                blended[bone] = blended.get(bone, 0.0) + weight * share
        smoothed[vertex] = blended
    return smoothed


def _limit(weights):
    """Strongest MAX_INFLUENCES entries, renormalised to sum 1."""
    top = sorted(weights.items(), key=lambda item: -item[1])[:MAX_INFLUENCES]
    total = sum(weight for _, weight in top)
    return {bone: weight / total for bone, weight in top}


def _normalised(weights):
    total = sum(weights.values())
    return {bone: weight / total for bone, weight in weights.items()}


def _same(a, b):
    return a.keys() == b.keys() and all(abs(a[bone] - b[bone]) <= _UNCHANGED_TOLERANCE for bone in a)


def _entries(weights):
    entries = []
    for bone, weight in weights.items():
        entry = unreal.GeometryScriptBoneWeight()
        entry.set_editor_property("bone_index", bone)
        entry.set_editor_property("weight", weight)
        entries.append(entry)
    return entries


def _body_named_weights(body_mesh_path, body):
    """{vertex: {bone name: weight}} of a body type's mesh, cached per path and stamp."""
    key = (body_mesh_path, assets.read_stamp(body_mesh_path))
    if key not in _body_weight_cache:
        names, _ = bone_table(body)
        weights = _read_weights(body, _vertex_ids(body))
        _body_weight_cache.clear()
        _body_weight_cache[key] = {v: {names[b]: w for b, w in d.items()} for v, d in weights.items()}
    return _body_weight_cache[key]


def _triangle_vertices(body, triangle_id):
    result = _QUERIES.get_triangle_indices(body, triangle_id)
    for value in result if isinstance(result, tuple) else (result,):
        if not isinstance(value, (bool, int)) and hasattr(value, "x") and hasattr(value, "z"):
            return int(value.x), int(value.y), int(value.z)
    raise RuntimeError("get_triangle_indices returned no triangle")


def _barycentric(hit, corners, position, body):
    """Barycentric weights of the nearest-point hit; falls back to the closest corner when the hit has none."""
    try:
        b = hit.get_editor_property("bary_coords")
        total = b.x + b.y + b.z
        if total > 1e-6:
            return b.x / total, b.y / total, b.z / total
    except Exception:  # noqa: BLE001 - field name differs between engine versions
        pass
    from . import clearance
    distances = [(position - clearance._position(body, v)).length() for v in corners]
    nearest = distances.index(min(distances))
    return tuple(1.0 if i == nearest else 0.0 for i in range(3))


def transfer_near_skin(mesh, current, names, body_mesh_path):
    """Blend the body's weights into `current` ({vertex: {bone index: weight}}) for vertices near the skin.

    t = 1 within TRANSFER_FULL_CM of the skin, fading to 0 over TRANSFER_FADE_CM; the blended zone is then
    smoothed TRANSFER_SMOOTH_RINGS times. Returns (weights, number of vertices blended).
    """
    from . import clearance
    body, bvh = clearance._body(body_mesh_path)
    body_weights = _body_named_weights(body_mesh_path, body)
    index_of = {name: index for index, name in enumerate(names)}
    keep_core = TRANSFER_KEEP == "core_twist"
    if keep_core:
        _, parents = bone_table(mesh)
        targets = core_targets(names, parents)
    end = TRANSFER_FULL_CM + TRANSFER_FADE_CM
    result = dict(current)
    blended = set()
    for vertex, own in current.items():
        position = clearance._position(mesh, vertex)
        hit = clearance._first(
            clearance._SPATIAL.find_nearest_point_on_mesh(body, bvh, position, clearance._OPTIONS),
            unreal.GeometryScriptTrianglePoint,
        )
        distance = (position - hit.position).length()
        if distance >= end:
            continue
        t = 1.0 if distance <= TRANSFER_FULL_CM else (end - distance) / TRANSFER_FADE_CM
        corners = _triangle_vertices(body, hit.triangle_id)
        bary = _barycentric(hit, corners, position, body)
        taken = {}
        for corner, share in zip(corners, bary):
            for name, weight in body_weights.get(corner, {}).items():
                index = index_of.get(name)
                if index is not None and share > 0.0:
                    if keep_core:
                        index = targets[index]  # helper / corrective bone -> nearest core ancestor
                    taken[index] = taken.get(index, 0.0) + weight * share
        if not taken:
            continue
        taken = _normalised(taken)
        mixed = {bone: weight * (1.0 - t) for bone, weight in own.items()}
        for bone, weight in taken.items():
            mixed[bone] = mixed.get(bone, 0.0) + weight * t
        result[vertex] = mixed
        blended.add(vertex)
    if blended and TRANSFER_SMOOTH_RINGS:
        adjacent = _neighbours(mesh)
        for _ in range(TRANSFER_SMOOTH_RINGS):
            smoothed = _smooth(result, adjacent)
            for vertex in blended:
                result[vertex] = smoothed[vertex]
    return result, len(blended)


def clean(mesh, label="", body_mesh_path=None):
    """Re-skin `mesh` (a DynamicMesh with bones and weights) to core bones only; returns (mesh, summary).

    Raises when a vertex has no weights at all. The summary counts vertices, vertices that carried
    non-core weight, and vertices rewritten. With `body_mesh_path` (harvest) and config.FIT["transfer"],
    vertices near the body skin then take the body's weights over all bones (see transfer_near_skin);
    the result is no longer core-only there.
    """
    started = time.time()
    names, parents = bone_table(mesh)
    targets = core_targets(names, parents)
    ids = _vertex_ids(mesh)
    original = _read_weights(mesh, ids)
    missing = sum(1 for w in original.values() if not w)
    if missing:
        raise RuntimeError("%d vertices have no bone weights" % missing)
    touched = sum(1 for w in original.values() if any(targets[b] != b for b in w))
    current = {v: _remap(w, targets) for v, w in original.items()}
    if SMOOTH_PASSES:
        adjacent = _neighbours(mesh)
        for _ in range(SMOOTH_PASSES):
            current = _smooth(current, adjacent)
    transferred = 0
    if body_mesh_path and TRANSFER_ENABLED:
        current, transferred = transfer_near_skin(mesh, current, names, body_mesh_path)
    rewritten = 0
    for vertex, weights in current.items():
        final = _limit(weights)
        if _same(final, _normalised(original[vertex])):
            continue
        mesh, _ = _WEIGHTS.set_vertex_bone_weights(mesh, vertex, _entries(final))
        rewritten += 1
    summary = {"vertices": len(ids), "had_non_core": touched, "rewritten": rewritten, "near_skin_transfer": transferred}
    rpt.log(config.LOG_FIT, "%s weights cleaned %s (%.1f s)" % (label, summary, time.time() - started))
    return mesh, summary


def audit(mesh):
    """Read-only check of a DynamicMesh's weights.

    Returns {"vertices", "missing" (no weights), "non_core" (vertices with weight on a non-core bone),
    "max_influences"}.
    """
    names, _ = bone_table(mesh)
    ids = _vertex_ids(mesh)
    weights = _read_weights(mesh, ids)
    return {
        "vertices": len(ids),
        "missing": sum(1 for w in weights.values() if not w),
        "non_core": sum(1 for w in weights.values() if any(not is_core(names[b]) for b in w)),
        "max_influences": max((len(w) for w in weights.values()), default=0),
    }
