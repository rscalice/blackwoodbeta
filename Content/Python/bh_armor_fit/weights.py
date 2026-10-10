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

_WEIGHTS = unreal.GeometryScript_BoneWeights
_QUERIES = unreal.GeometryScript_MeshQueries


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


def clean(mesh, label=""):
    """Re-skin `mesh` (a DynamicMesh with bones and weights) to core bones only; returns (mesh, summary).

    Raises when a vertex has no weights at all. The summary counts vertices, vertices that carried
    non-core weight, and vertices rewritten.
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
    rewritten = 0
    for vertex, weights in current.items():
        final = _limit(weights)
        if _same(final, _normalised(original[vertex])):
            continue
        mesh, _ = _WEIGHTS.set_vertex_bone_weights(mesh, vertex, _entries(final))
        rewritten += 1
    summary = {"vertices": len(ids), "had_non_core": touched, "rewritten": rewritten}
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
