"""Read-only: where does a final armor mesh's skin weight really sit?

    py import importlib, bh_armor_fit.inspect_weights as w; importlib.reload(w); w.run()
    py import importlib, bh_armor_fit.inspect_weights as w; importlib.reload(w); w.run(["SKM_Platemail_Gloves_MaleNormal"])

Per piece (log tag [BH_Weights]): bones carrying weight (vertex count, total weight), bones that do not exist on the
MaleNormal body mesh, vertices without weight, the top bones per side (x > 0 / x < 0), and the final mesh's own reference
skeleton compared with the body's (missing / extra names, parent differences, ref-pose scale ~0 or position far from the body's).
Nothing is written.
"""
import unreal

from . import meshes, weights

BODY_PATH = "/Game/BlackwoodHollow/Characters/BodyTypes/MaleNormal/Built_Fit/MHC_MaleNormal/Body/SKM_MHC_MaleNormal_BodyMesh"
ARMOR_ROOT = "/Game/BlackwoodHollow/Equipment/Armor"
DEFAULT_MESHES = [
    "SKM_Platemail_Gloves_MaleNormal",
    "SKM_BasicLeather_Boots_MaleNormal",
    "SKM_SpellCaster_Gloves_MaleNormal",
    "SKM_StarterCloth_Gloves_MaleNormal",
]
SCALE_MIN = 0.05
POSITION_TOLERANCE_CM = 0.5
_WEIGHTS = unreal.GeometryScript_BoneWeights
_LOG = "[BH_Weights]"


def _log(text):
    unreal.log("%s %s" % (_LOG, text))


def _warn(text):
    unreal.log_warning("%s %s" % (_LOG, text))


def _find(name):
    """Load by short name (SKM_Set_Slot_MaleNormal) under Equipment/Armor/<Set>/, else search the registry."""
    parts = name.split("_")
    if len(parts) >= 2:
        asset = unreal.load_asset("%s/%s/%s" % (ARMOR_ROOT, parts[1], name))
        if asset is not None:
            return asset
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    for data in registry.get_assets_by_class(unreal.TopLevelAssetPath("/Script/Engine", "SkeletalMesh")):
        if str(data.asset_name) == name:
            return data.get_asset()
    return None


def _skeleton(mesh):
    """({name: parent name or None}, {name: Transform}) of the bones a DynamicMesh carries."""
    _, infos = _WEIGHTS.get_all_bones_info(mesh)
    infos = sorted(infos, key=lambda i: i.index)
    names = [str(i.name) for i in infos]
    parents = {names[k]: (names[i.parent_index] if i.parent_index >= 0 else None) for k, i in enumerate(infos)}
    local = {str(i.name): i.local_transform for i in infos}
    return names, parents, local


def _component_translations(names, parents, local):
    """Component-space ref-pose position of every bone (composed from the local transforms)."""
    cache = {}

    def get(name):
        if name not in cache:
            parent = parents[name]
            cache[name] = local[name] if parent is None else local[name].multiply(get(parent))
        return cache[name]

    return {name: get(name).translation for name in names}


def _vertex_data(mesh, names):
    ids = weights._vertex_ids(mesh)
    raw = weights._read_weights(mesh, ids)
    per_bone = {}
    zero = []
    side_bones = {"x>0": {}, "x<0": {}}
    for vertex, entries in raw.items():
        position = mesh_position(mesh, vertex)
        side = side_bones["x>0" if position.x > 0 else "x<0"]
        total = sum(entries.values())
        if total <= 1e-6:
            zero.append(vertex)
        for bone, weight in entries.items():
            name = names[bone] if bone < len(names) else "<bad index %d>" % bone
            count, amount = per_bone.get(name, (0, 0.0))
            per_bone[name] = (count + 1, amount + weight)
            count, amount = side.get(name, (0, 0.0))
            side[name] = (count + 1, amount + weight)
    return len(ids), per_bone, zero, side_bones


def mesh_position(mesh, vertex):
    result = unreal.GeometryScript_MeshQueries.get_vertex_position(mesh, vertex)
    for value in result if isinstance(result, tuple) else (result,):
        if hasattr(value, "x") and hasattr(value, "z"):
            return value
    raise RuntimeError("get_vertex_position returned no position")


def _report_skeleton(label, names, parents, local, body):
    body_names, body_parents, body_local = body
    missing = [n for n in body_names if n not in set(names)]
    extra = [n for n in names if n not in set(body_names)]
    _log("%s ref skeleton: %d bones (body %d); missing vs body %d, extra vs body %d; order identical: %s" % (
        label, len(names), len(body_names), len(missing), len(extra), names == body_names))
    if missing:
        _warn("%s   missing vs body: %s" % (label, missing[:30]))
    if extra:
        _warn("%s   extra vs body: %s" % (label, extra[:30]))
    for name in names:
        if name in body_parents and parents[name] != body_parents[name]:
            _warn("%s   parent differs: %s -> %s (body: %s)" % (label, name, parents[name], body_parents[name]))
    mine = _component_translations(names, parents, local)
    theirs = _component_translations(body_names, body_parents, body_local)
    for name in names:
        scale = local[name].scale3d
        smallest = min(abs(scale.x), abs(scale.y), abs(scale.z))
        if smallest < SCALE_MIN:
            _warn("%s   ZERO-ish ref-pose scale on %s: %.3f %.3f %.3f" % (label, name, scale.x, scale.y, scale.z))
        if name in theirs and (mine[name] - theirs[name]).length() > POSITION_TOLERANCE_CM:
            _warn("%s   ref pose differs from body: %s by %.2f cm" % (label, name, (mine[name] - theirs[name]).length()))


def inspect(name, body):
    asset = _find(name)
    if asset is None:
        _warn("%s not found" % name)
        return
    mesh = meshes.read(asset)
    names, parents, local = _skeleton(mesh)
    count, per_bone, zero, sides = _vertex_data(mesh, names)
    body_names = set(body[0])
    _log("== %s  %d vertices, %d bones  %s" % (name, count, len(names), asset.get_path_name()))
    _log("%s bones carrying weight (%d):" % (name, len(per_bone)))
    for bone, (n, total) in sorted(per_bone.items(), key=lambda item: -item[1][1]):
        flag = "" if bone in body_names else "   <-- NOT ON BODY"
        _log("    %-28s %6d verts  total weight %9.2f%s" % (bone, n, total, flag))
    foreign = [b for b in per_bone if b not in body_names]
    (_warn if foreign else _log)("%s weighted bones not on the body: %s" % (name, foreign or "none"))
    (_warn if zero else _log)("%s vertices with zero total weight: %d %s" % (name, len(zero), zero[:10] if zero else ""))
    for side, table in sides.items():
        top = sorted(table.items(), key=lambda item: -item[1][1])[:5]
        _log("%s top bones %s: %s" % (name, side, ", ".join("%s (%d v, %.1f)" % (b, n, t) for b, (n, t) in top) or "no vertices"))
    _report_skeleton(name, names, parents, local, body)


def run(mesh_names=None):
    body_asset = unreal.load_asset(BODY_PATH)
    if body_asset is None:
        _warn("body mesh missing: %s" % BODY_PATH)
        return
    body = _skeleton(meshes.read(body_asset))
    for name in mesh_names or DEFAULT_MESHES:
        try:
            inspect(name, body)
        except Exception as error:  # noqa: BLE001
            _warn("%s failed: %s" % (name, error))
