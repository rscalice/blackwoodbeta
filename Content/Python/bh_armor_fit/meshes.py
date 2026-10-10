"""GeometryScript and material-slot helpers shared by rigging and harvest."""
import unreal

from . import assets

_ASSET_UTILS = unreal.GeometryScript_AssetUtils
_SUCCESS = unreal.GeometryScriptOutcomePins.SUCCESS
BIND_POSE_TOLERANCE_CM = 0.01


def read(asset):
    """LOD0 of a static or skeletal mesh as a DynamicMesh (material IDs are section indices)."""
    options = unreal.GeometryScriptCopyMeshFromAssetOptions()
    lod = unreal.GeometryScriptMeshReadLOD()
    mesh = unreal.DynamicMesh()
    if isinstance(asset, unreal.StaticMesh):
        mesh, outcome = _ASSET_UTILS.copy_mesh_from_static_mesh_v2(asset, mesh, options, lod, True)
    else:
        mesh, outcome = _ASSET_UTILS.copy_mesh_from_skeletal_mesh(asset, mesh, options, lod)
    if outcome != _SUCCESS:
        raise RuntimeError("Could not read mesh " + asset.get_path_name())
    return mesh


def slots(asset):
    """[(slot name, material)] of a static or skeletal mesh, in section order."""
    entries = asset.static_materials if isinstance(asset, unreal.StaticMesh) else asset.materials
    return [(str(e.material_slot_name), e.material_interface) for e in entries]


def vertex_count(mesh):
    return unreal.GeometryScript_MeshQueries.get_vertex_count(mesh)


def triangle_count(mesh):
    """Live triangles (MeshQueries.get_num_triangle_i_ds counts IDs, including ones freed by deletion)."""
    return mesh.get_triangle_count()


def bounds(mesh):
    return unreal.GeometryScript_MeshQueries.get_mesh_bounding_box(mesh)


def bounds_within(box_a, box_b, tolerance):
    """True when every corner coordinate of the two boxes differs by at most `tolerance` (cm)."""
    pairs = ((box_a.min, box_b.min), (box_a.max, box_b.max))
    return all(abs(a.get_editor_property(c) - b.get_editor_property(c)) <= tolerance for a, b in pairs for c in "xyz")


def keep_sections(mesh, keep_ids, section_count):
    """Delete every triangle whose material ID (section index) is not in `keep_ids`."""
    for section in range(section_count):
        if section not in keep_ids:
            mesh, _ = unreal.GeometryScript_Materials.delete_triangles_by_material_id(mesh, section)
    return mesh


def compact_sections(mesh, materials):
    """Renumber the remaining material IDs from 0; returns (mesh, materials in new order)."""
    mesh, compacted = unreal.GeometryScript_Materials.compact_material_i_ds(mesh, materials)
    return mesh, list(compacted)


def local_bone_positions(mesh):
    """{bone name: local reference-pose position (cm)} of the skeleton a DynamicMesh carries."""
    _, infos = unreal.GeometryScript_BoneWeights.get_all_bones_info(mesh)
    return {str(i.name): i.local_transform.translation for i in infos}


def bind_pose_delta(positions_a, positions_b):
    """Largest per-bone difference (cm) between two local_bone_positions() results; inf when the bones differ."""
    if positions_a.keys() != positions_b.keys():
        return float("inf")
    return max((positions_a[name] - positions_b[name]).length() for name in positions_a)


def copy_bones(source_mesh, mesh):
    """`mesh` carrying the skeleton (names, hierarchy and reference pose) of `source_mesh`."""
    return unreal.GeometryScript_BoneWeights.copy_bones_from_mesh(source_mesh, mesh)


def write_skeletal(mesh, package, skeleton, materials, slot_names):
    """Create the skeletal mesh at `package` (or overwrite it in place) with the reference pose `mesh` carries.

    The reference pose must be the bone proportions on `mesh` (the body the armor was fitted to), not the
    Skeleton asset's own reference pose, which belongs to a different MetaHuman body. Overwriting in place
    keeps the asset's existing reference skeleton, so when that is wrong the asset is deleted and created
    fresh (soft references by path survive; hard references must be re-pointed by the caller). The check
    reads the ASSET's reference pose, not the written mesh data.
    """
    if assets.exists(package):
        target = assets.load(package)
        target.modify(True)
        options = unreal.GeometryScriptCopyMeshToAssetOptions()
        options.set_editor_property("replace_materials", True)
        options.set_editor_property("new_materials", materials)
        options.set_editor_property("new_material_slot_names", slot_names)
        _, outcome = _ASSET_UTILS.copy_mesh_to_skeletal_mesh(mesh, target, options, unreal.GeometryScriptMeshWriteLOD())
        if outcome == _SUCCESS and asset_bind_pose_delta(target, mesh) <= BIND_POSE_TOLERANCE_CM:
            return target
        target = None
        if not unreal.EditorAssetLibrary.delete_asset(package):
            raise RuntimeError("Could not replace %s (wrong reference pose and delete failed)" % package)
    assets.ensure_folder(package.rsplit("/", 1)[0])
    options = unreal.GeometryScriptCreateNewSkeletalMeshAssetOptions()
    options.set_editor_property("use_mesh_bone_proportions", True)
    # "materials" is a TMap<FName, UMaterialInterface*> (slot name -> material), in slot order.
    options.set_editor_property("materials", _slot_material_map(slot_names, materials))
    target, outcome = unreal.GeometryScript_NewAssetUtils.create_new_skeletal_mesh_asset_from_mesh(
        mesh, skeleton, package, options
    )
    if outcome != _SUCCESS or target is None:
        raise RuntimeError("Could not write skeletal mesh " + package)
    rename_slots(target, slot_names)
    delta = asset_bind_pose_delta(target, mesh)
    if delta > BIND_POSE_TOLERANCE_CM:
        raise RuntimeError("%s has the wrong reference pose (bones off by %.3f cm)" % (package, delta))
    return target


def asset_bind_pose_delta(skeletal_mesh, mesh):
    """Largest difference (cm) between the asset's reference-pose local bone positions and the bones on `mesh`.

    Compares the bones both have (build outputs can carry one extra helper bone); inf when none are shared.
    """
    wanted = local_bone_positions(mesh)
    component = unreal.SkeletalMeshComponent()
    component.set_skeletal_mesh_asset(skeletal_mesh)
    deltas = [
        (component.get_ref_pose_position(index) - wanted[name]).length()
        for index, name in ((i, str(component.get_bone_name(i))) for i in range(component.get_num_bones()))
        if name in wanted
    ]
    return max(deltas) if deltas else float("inf")


def _slot_material_map(slot_names, materials):
    """Slot name -> material, keeping order; duplicate names get a numeric suffix so no slot is lost."""
    mapping = {}
    for index, (name, material) in enumerate(zip(slot_names, materials)):
        key = str(name) or "Slot_%d" % index
        if key in mapping:
            key = "%s_%d" % (key, index)
        mapping[unreal.Name(key)] = material
    return mapping


def rename_slots(skeletal_mesh, names):
    """Give the mesh's material slots these names (in order)."""
    entries = []
    for entry, name in zip(skeletal_mesh.materials, names):
        renamed = entry.copy()
        renamed.set_editor_property("material_slot_name", name)
        entries.append(renamed)
    skeletal_mesh.modify(True)
    skeletal_mesh.set_editor_property("materials", entries)


def apply_material_overrides(skeletal_mesh, overrides):
    """Set material instances by slot name (copy each entry, then set the whole list back).

    Returns the override slot names that matched no slot.
    """
    entries = []
    matched = set()
    for entry in skeletal_mesh.materials:
        updated = entry.copy()
        name = str(entry.material_slot_name)
        if name in overrides:
            updated.set_editor_property("material_interface", assets.load(overrides[name]))
            matched.add(name)
        entries.append(updated)
    skeletal_mesh.modify(True)
    skeletal_mesh.set_editor_property("materials", entries)
    return sorted(set(overrides) - matched)
