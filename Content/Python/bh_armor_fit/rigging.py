"""Rig Static Pieces: turn static meshes into skinned skeletal meshes on the canonical skeleton.

Two modes per config row:
  RigidBone    every vertex 100% on one bone (helms, masks, caps); the piece moves rigidly with it.
  BodyWeights  weights transferred from the built body + face meshes, smoothed, limited to 4 influences.
The Fab original is never touched; the result is a new asset under Equipment/Rigged/<Set>/.
"""
import time

import unreal

from . import assets, checks, config, meshes, weights
from . import report as rpt

_WEIGHTS = unreal.GeometryScript_BoneWeights
BOUNDS_TOLERANCE_CM = 0.1


def rig_static_pieces(pieces=None, dry_run=False, force=False):
    """Rig every piece with a RigMode (or only the rows named in `pieces`) that is not up to date (all with `force`)."""
    checks.preflight()
    run = rpt.Report("rig_static_pieces", dry_run)
    rig_into(run, pieces, dry_run, force)
    return run.finish(config.LOG_RIG)


def rig_into(run, names, dry_run, force=False):
    """Rig the selected pieces, recording each result in `run` (shared with Fit Armor)."""
    owners = _rig_owners(config.load_pieces(), names)
    if not owners:
        rpt.warn(config.LOG_RIG, "No pieces to rig.")
        return
    with rpt.Progress(len(owners), "Rigging static pieces") as progress:
        for number, piece in enumerate(owners, 1):
            if progress.cancelled:
                run.add(piece.name, "SKIP", "cancelled")
                break
            progress.step("%s (%s)" % (piece.name, piece.rig_mode))
            label = "%d/%d %s %s" % (number, len(owners), piece.name, piece.rig_mode)
            _rig_or_report(piece, label, run, dry_run, force)


def _rig_owners(all_pieces, names):
    """One piece per distinct rigged asset (the first row that owns it), filtered by row name."""
    owners = {}
    for piece in all_pieces:
        if not piece.rigged_path:
            continue
        selected = names is None or any(
            q.name in names for q in all_pieces if q.rigged_path == piece.rigged_path
        )
        if selected:
            owners.setdefault(piece.rigged_path, piece)
    return list(owners.values())


def _expected_stamp(piece):
    inputs = [piece.source_mesh, config.CANONICAL_SKELETON] + sorted(piece.material_overrides.values())
    if piece.rig_mode == config.RIG_BODY_WEIGHTS:
        inputs += [piece.source_body, _face_mesh_package(piece.source_body)]
    return assets.stamp_of(
        piece.rig_mode,
        piece.rigid_bone,
        weights.SIGNATURE,
        sorted(piece.material_overrides.items()),
        assets.input_stamp(*inputs),
    )


def _face_mesh_package(body_package):
    return body_package.replace("/Body/", "/Face/").replace("_BodyMesh", "_FaceMesh")


def _rig_or_report(piece, label, run, dry_run, force):
    if piece.locked and assets.exists(piece.rigged_path):
        run.add(piece.name, "SKIP", "locked")
        rpt.log(config.LOG_RIG, "%s ... locked, skipped" % label)
        return
    if not force and assets.read_stamp(piece.rigged_path) == _expected_stamp(piece):
        run.add(piece.name, "SKIP", "up to date")
        rpt.log(config.LOG_RIG, "%s ... up to date, skipped" % label)
        return
    if dry_run:
        run.add(piece.name, "PLAN", "would rig %s -> %s" % (piece.source_mesh, piece.rigged_path))
        rpt.log(config.LOG_RIG, "%s ... would rig -> %s" % (label, piece.rigged_path))
        return
    started = time.time()
    try:
        detail = _rig_one(piece)
    except Exception as exc:  # log and continue with the next piece
        run.add(piece.name, "FAIL", str(exc))
        rpt.error(config.LOG_RIG, "%s ... FAIL: %s" % (label, exc))
        return
    seconds = round(time.time() - started, 1)
    run.add(piece.name, "OK", detail, seconds)
    rpt.log(config.LOG_RIG, "%s ... OK (%s s) %s" % (label, seconds, detail))


def _first(result):
    return result[0] if isinstance(result, tuple) else result


def _rig_one(piece):
    source = assets.load(piece.source_mesh)
    skeleton = assets.load(config.CANONICAL_SKELETON)
    # The bones come from the source body, not the Skeleton asset: the Skeleton's own reference pose
    # belongs to a different body and would leave the piece floating off the bind pose.
    mesh = meshes.copy_bones(meshes.read(assets.load(piece.source_body)), meshes.read(source))
    mesh = _first(_WEIGHTS.mesh_create_bone_weights(mesh, True))
    if piece.rig_mode == config.RIG_RIGID_BONE:
        mesh = _weight_rigid(mesh, piece.rigid_bone)
    else:
        mesh = _weight_from_body(mesh, piece)
    mesh, _ = weights.clean(mesh, piece.name)
    source_box = meshes.bounds(meshes.read(source))
    if not meshes.bounds_within(meshes.bounds(mesh), source_box, BOUNDS_TOLERANCE_CM):
        raise RuntimeError("bounds changed by more than %s cm" % BOUNDS_TOLERANCE_CM)
    named = meshes.slots(source)
    materials = [
        assets.load(piece.material_overrides[name]) if name in piece.material_overrides else material
        for name, material in named
    ]
    rigged = meshes.write_skeletal(mesh, piece.rigged_path, skeleton, materials, [name for name, _ in named])
    assets.write_stamp(rigged, _expected_stamp(piece))
    assets.save([piece.rigged_path])
    return "%d verts, core-bone weights, max %d influences, %d materials" % (
        meshes.vertex_count(mesh), weights.MAX_INFLUENCES, len(materials)
    )


def _weight_rigid(mesh, bone_name):
    result = _WEIGHTS.get_bone_index(mesh, bone_name)
    if not result[1]:
        raise RuntimeError("bone '%s' not on the canonical skeleton" % bone_name)
    weight = unreal.GeometryScriptBoneWeight()
    weight.set_editor_property("bone_index", result[2])
    weight.set_editor_property("weight", 1.0)
    return _WEIGHTS.set_all_vertex_bone_weights(mesh, [weight])


def _weight_from_body(mesh, piece):
    """Transfer weights from the built body merged with its face mesh (the body stops at the neck)."""
    reference = meshes.read(assets.load(piece.source_body))
    face_package = _face_mesh_package(piece.source_body)
    if assets.exists(face_package):
        reference = unreal.GeometryScript_MeshEdits.append_mesh(
            reference, meshes.read(assets.load(face_package)), unreal.Transform()
        )
    else:
        rpt.warn(config.LOG_RIG, "%s: face mesh %s missing; head vertices may snap to the neck" % (piece.name, face_package))
    options = unreal.GeometryScriptTransferBoneWeightsOptions()
    options.set_editor_property("num_smoothing_iterations", 3)
    options.set_editor_property("smoothing_strength", 0.5)
    return _WEIGHTS.transfer_bone_weights_from_mesh(reference, mesh, options)
