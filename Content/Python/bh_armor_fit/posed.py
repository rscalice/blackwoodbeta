"""Posed check (REPORT ONLY): does the body poke out of a finished piece when the character moves?

Runs after a piece is harvested and its mask baked, only when config.FIT["posed_check"] is true (or
fit_armor(posed_check=True)). It never changes an asset and a failure here never fails the fit.

A few test poses are built with simple forward kinematics from the bind pose of the garment's own skeleton
(each pose = world-space rotations of joint bones about their bind joint positions, propagated down the
hierarchy); body and garment are then linear-blend skinned with their own weights. Every body vertex the mask
marks as covered is tested against the posed garment surface: it "pokes out" when it lies on the outer side of the
nearest garment triangle by more than POKE_TOLERANCE_CM. The result is a count per pose, e.g.
    arms_out: 12 / 4210 covered body vertices poke out (max 1.8 cm)

Poses: arms_out (shoulders abduct, sign chosen so the elbows move away from the midline), flex_pos / flex_neg
(shoulder, elbow, hip, knee and waist rotated +/- 60 degrees about the left-right axis).
This is a coarse screening tool (no corrective bone drivers, rotations only), meant to rank pieces, not to be exact.
"""
import math
import time

import unreal

from . import clearance, config, weights
from . import report as rpt

POKE_TOLERANCE_CM = 0.3
REACH_CM = 6.0
FLEX_DEGREES = 60.0
ABDUCT_DEGREES = 70.0

_EDIT = unreal.GeometryScript_MeshEdits
_QUERIES = unreal.GeometryScript_MeshQueries
_SPATIAL = unreal.GeometryScript_MeshSpatial

_FLEX_BONES = ("spine_02", "upperarm_l", "upperarm_r", "lowerarm_l", "lowerarm_r", "thigh_l", "thigh_r", "calf_l", "calf_r")


def _rotation_matrix(axis, degrees):
    x, y, z = axis
    n = math.sqrt(x * x + y * y + z * z) or 1.0
    x, y, z = x / n, y / n, z / n
    a = math.radians(degrees)
    c, s = math.cos(a), math.sin(a)
    t = 1.0 - c
    return (
        (t * x * x + c, t * x * y - s * z, t * x * z + s * y),
        (t * x * y + s * z, t * y * y + c, t * y * z - s * x),
        (t * x * z - s * y, t * y * z + s * x, t * z * z + c),
    )


def _mul(a, b):
    return tuple(tuple(sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)) for i in range(3))


def _apply(m, v):
    return tuple(sum(m[i][k] * v[k] for k in range(3)) for i in range(3))


def _quat_matrix(q):
    x, y, z, w = q.x, q.y, q.z, q.w
    return (
        (1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)),
        (2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)),
        (2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)),
    )


def _bind_joints(mesh):
    """({bone name: world bind position}, {bone name: parent name or ''}, bone names in index order)."""
    _, infos = unreal.GeometryScript_BoneWeights.get_all_bones_info(mesh)
    infos = sorted(infos, key=lambda i: i.index)
    rotations, positions, names, parents = [], [], [], []
    for info in infos:
        local = info.local_transform
        t = (local.translation.x, local.translation.y, local.translation.z)
        r = _quat_matrix(local.rotation)
        if info.parent_index < 0:
            rotations.append(r)
            positions.append(t)
        else:
            rotations.append(_mul(rotations[info.parent_index], r))
            p = _apply(rotations[info.parent_index], t)
            pp = positions[info.parent_index]
            positions.append((pp[0] + p[0], pp[1] + p[1], pp[2] + p[2]))
        names.append(str(info.name))
        parents.append(info.parent_index)
    return names, parents, positions


def _pose_transforms(names, parents, positions, rotations_by_bone):
    """Per bone index: (R, t) mapping bind-space points to posed space."""
    ident = ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))
    result = []
    for index, name in enumerate(names):
        r_extra = rotations_by_bone.get(name, ident)
        p = positions[index]
        rp = _apply(r_extra, p)
        t_local = (p[0] - rp[0], p[1] - rp[1], p[2] - rp[2])  # x -> R(x - p) + p
        if parents[index] < 0:
            result.append((r_extra, t_local))
        else:
            r_parent, t_parent = result[parents[index]]
            rt = _apply(r_parent, t_local)
            result.append((_mul(r_parent, r_extra), (rt[0] + t_parent[0], rt[1] + t_parent[1], rt[2] + t_parent[2])))
    return result


def _skin(positions, bone_weights, transforms):
    posed = {}
    for vertex, p in positions.items():
        acc = [0.0, 0.0, 0.0]
        total = 0.0
        for bone, weight in bone_weights.get(vertex, {}).items():
            r, t = transforms[bone]
            q = _apply(r, p)
            acc[0] += weight * (q[0] + t[0])
            acc[1] += weight * (q[1] + t[1])
            acc[2] += weight * (q[2] + t[2])
            total += weight
        posed[vertex] = (acc[0] / total, acc[1] / total, acc[2] / total) if total > 1e-6 else p
    return posed


def _poses(names, positions):
    index = {n: i for i, n in enumerate(names)}
    need = ("upperarm_l", "upperarm_r", "spine_02")
    if any(n not in index for n in need):
        raise RuntimeError("skeleton lacks %s" % [n for n in need if n not in index])
    left, right = positions[index["upperarm_l"]], positions[index["upperarm_r"]]
    lateral = (right[0] - left[0], right[1] - left[1], right[2] - left[2])
    norm = math.sqrt(sum(c * c for c in lateral)) or 1.0
    lateral = tuple(c / norm for c in lateral)
    up = (0.0, 0.0, 1.0)
    forward = (lateral[1] * up[2] - lateral[2] * up[1], lateral[2] * up[0] - lateral[0] * up[2], lateral[0] * up[1] - lateral[1] * up[0])

    poses = {}
    for sign, label in ((1.0, "flex_pos"), (-1.0, "flex_neg")):
        poses[label] = {b: _rotation_matrix(lateral, sign * FLEX_DEGREES) for b in _FLEX_BONES if b in index}

    # arms_out: pick the sign that moves each elbow away from the midline
    mid = tuple((left[i] + right[i]) / 2.0 for i in range(3))
    arms = {}
    for side in ("l", "r"):
        shoulder, elbow_name = "upperarm_" + side, "lowerarm_" + side
        if elbow_name not in index:
            continue
        s = positions[index[shoulder]]
        e = positions[index[elbow_name]]
        best, best_gap = None, -1e9
        for sign in (1.0, -1.0):
            m = _rotation_matrix(forward, sign * ABDUCT_DEGREES)
            moved = _apply(m, tuple(e[i] - s[i] for i in range(3)))
            point = tuple(s[i] + moved[i] for i in range(3))
            gap = sum(abs(point[i] - mid[i]) * abs(lateral[i]) for i in range(3))
            if gap > best_gap:
                best, best_gap = m, gap
        arms[shoulder] = best
    poses["arms_out"] = arms
    return poses


def check(garment, body_mesh_path, flags, body_data, label=""):
    """Report poke-through of the covered body vertices per pose; returns a short string (never raises)."""
    started = time.time()
    try:
        return _check(garment, body_mesh_path, flags, body_data, label, started)
    except Exception as exc:  # noqa: BLE001 - report only, must never fail a fit
        rpt.warn(config.LOG_FIT, "%s posed check skipped: %s" % (label, exc))
        return "posed check skipped (%s)" % exc


def _check(garment, body_mesh_path, flags, body_data, label, started):
    body, _bvh = clearance._body(body_mesh_path)
    g_names, g_parents, g_joints = _bind_joints(garment)
    b_names, b_parents, b_joints = _bind_joints(body)
    g_index = {n: i for i, n in enumerate(g_names)}
    poses = _poses(b_names, b_joints)

    g_bind = {v: tuple(c for c in (p.x, p.y, p.z)) for v, p in ((v, clearance._position(garment, v)) for v in clearance._vertex_ids(garment))}
    g_weights = weights._read_weights(garment, list(g_bind))
    b_bind = body_data["positions"]
    b_weights = weights._read_weights(body, list(b_bind))
    covered = [vertices[0] for group, vertices in enumerate(body_data["members"]) if flags[group]]
    if not covered:
        return "posed check: nothing covered"

    parts = []
    try:
        for pose_name, rotations in poses.items():
            g_transforms = _pose_transforms(g_names, g_parents, g_joints, rotations)
            b_rot = rotations  # same bone names on the body skeleton
            b_transforms = _pose_transforms(b_names, b_parents, b_joints, b_rot)
            g_posed = _skin(g_bind, g_weights, g_transforms)
            b_posed = _skin({v: b_bind[v] for v in covered}, {v: b_weights.get(v, {}) for v in covered}, b_transforms)
            for vertex, p in g_posed.items():
                garment, _ = _EDIT.set_vertex_position(garment, vertex, unreal.Vector(*p), True)
            bvh = clearance._first(_SPATIAL.build_bvh_for_mesh(garment, None), unreal.GeometryScriptDynamicMeshBVH)
            poking, worst = 0, 0.0
            for vertex in covered:
                p = b_posed[vertex]
                hit = clearance._first(
                    _SPATIAL.find_nearest_point_on_mesh(garment, bvh, unreal.Vector(*p), clearance._OPTIONS),
                    unreal.GeometryScriptTrianglePoint,
                )
                offset = (p[0] - hit.position.x, p[1] - hit.position.y, p[2] - hit.position.z)
                if math.sqrt(sum(c * c for c in offset)) > REACH_CM:
                    continue
                normal = clearance._first(_QUERIES.get_triangle_face_normal(garment, hit.triangle_id), unreal.Vector)
                outside = offset[0] * normal.x + offset[1] * normal.y + offset[2] * normal.z
                if outside > POKE_TOLERANCE_CM:
                    poking += 1
                    worst = max(worst, outside)
            parts.append("%s %d/%d (max %.1f cm)" % (pose_name, poking, len(covered), worst))
    finally:
        for vertex, p in g_bind.items():  # leave the mesh in its bind pose
            garment, _ = _EDIT.set_vertex_position(garment, vertex, unreal.Vector(*p), True)
    summary = "posed pokes: " + "; ".join(parts)
    rpt.log(config.LOG_FIT, "%s %s (%.1f s)" % (label, summary, time.time() - started))
    return summary
