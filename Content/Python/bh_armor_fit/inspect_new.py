"""Read-only report of the new armor meshes: per material section triangle count, Z range, skin-underlay guess.

Run from the editor Python console:
    from bh_armor_fit import inspect_new; inspect_new.run()
    inspect_new.run(["/Game/Fab/Equipment/BaseClothing/Basic_Cloth_Chest"])   # explicit paths

Nothing is written. A section is flagged SKIN? when its slot/material name says body/skin, or when it covers
more than half of the mesh height with a name that is not a garment.
"""
import unreal

from . import assets, config, meshes

DEFAULT_PATHS = [
    "/Game/Fab/Equipment/BaseClothing/Basic_Cloth_Chest",
    "/Game/Fab/Equipment/BaseClothing/Basic_Cloth_Pants",
    "/Game/Fab/Equipment/BaseClothing/Basic_Cloth_Boots",
    "/Game/Fab/Equipment/ClothArmor/Cloth_Full_Rigged",
]
_SKIN_WORDS = ("body", "skin", "synth", "nude", "underlay")
_MATERIAL_FUNCTIONS = unreal.GeometryScript_MeshMaterialFunctions
_QUERIES = unreal.GeometryScript_MeshQueries


def _section_stats(mesh, section_count):
    stats = {i: {"tris": 0, "zmin": 1e9, "zmax": -1e9} for i in range(section_count)}
    for tri in _QUERIES.get_all_triangle_ids(mesh) if hasattr(_QUERIES, "get_all_triangle_ids") else range(_QUERIES.get_num_triangle_i_ds(mesh)):
        if not _QUERIES.is_triangle_id(mesh, tri):
            continue
        material_id = _MATERIAL_FUNCTIONS.get_triangle_material_id(mesh, tri)
        if isinstance(material_id, tuple):
            material_id = material_id[0]
        entry = stats.get(material_id)
        if entry is None:
            continue
        indices = _QUERIES.get_triangle_indices(mesh, tri)
        if isinstance(indices, tuple):
            indices = indices[0]
        for vertex in (indices.x, indices.y, indices.z):
            position = _QUERIES.get_vertex_position(mesh, vertex)
            if isinstance(position, tuple):
                position = position[0]
            entry["zmin"] = min(entry["zmin"], position.z)
            entry["zmax"] = max(entry["zmax"], position.z)
        entry["tris"] += 1
    return stats


def run(paths=None):
    for path in paths or DEFAULT_PATHS:
        asset = unreal.load_asset(path)
        if asset is None:
            unreal.log_warning("%s missing: %s" % (config.LOG_FIT, path))
            continue
        mesh = meshes.read(asset)
        slots = meshes.slots(asset)
        box = meshes.bounds(mesh)
        height = max(box.max.z - box.min.z, 1e-6)
        unreal.log("%s %s: %d sections, %d tris, Z %.1f..%.1f" % (config.LOG_FIT, path, len(slots), meshes.triangle_count(mesh), box.min.z, box.max.z))
        stats = _section_stats(mesh, len(slots))
        for index, (name, material) in enumerate(slots):
            entry = stats[index]
            label = ("%s %s" % (name, material.get_name() if material else "")).lower()
            span = (entry["zmax"] - entry["zmin"]) / height if entry["tris"] else 0.0
            skin = any(word in label for word in _SKIN_WORDS) or (span > 0.5 and "cloth" not in label)
            unreal.log("    [%d] %-28s tris=%-6d Z %.1f..%.1f %s" % (
                index, name, entry["tris"], entry["zmin"] if entry["tris"] else 0.0, entry["zmax"] if entry["tris"] else 0.0,
                "SKIN?" if skin else ""))
