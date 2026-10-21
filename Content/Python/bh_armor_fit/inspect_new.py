"""Read-only report of the new armor meshes: per material section triangle count, Z range, skin-underlay guess.

Run from the editor Python console:
    from bh_armor_fit import inspect_new; inspect_new.run()
    inspect_new.run(["/Game/Fab/Equipment/BaseClothing/Basic_Cloth_Chest"])   # explicit paths

Nothing is written. A section is flagged SKIN? when its slot/material name says body/skin, or when it covers
more than half of the mesh height with a name that is not a garment.
"""
import unreal

from . import assets, clearance, config, meshes

DEFAULT_PATHS = [
    "/Game/Fab/Equipment/BaseClothing/Basic_Cloth_Chest",
    "/Game/Fab/Equipment/BaseClothing/Basic_Cloth_Pants",
    "/Game/Fab/Equipment/BaseClothing/Basic_Cloth_Boots",
    "/Game/Fab/Equipment/StarterClothArmor/Cloth_Full_Rigged",      # Boots
    "/Game/Fab/Equipment/StarterClothArmor/Cloth_Full_Rigged1",     # Gloves
    "/Game/Fab/Equipment/StarterClothArmor/Cloth_Full_Rigged12",    # Pants
    "/Game/Fab/Equipment/StarterClothArmor/Cloth_Full_Rigged123",   # Chest
]
# Authoring body of the Fab packs plus every built body type (the BodyTypes/Reference folder holds only textures).
AUTHORING_BODY = "/Game/Fab/Equipment/Polyphoria/CharacterParts_HeavyArmour/MH_Creator/SK_tal_nrw_base"
_SKIN_WORDS = ("body", "skin", "synth", "nude", "underlay")
_MATERIAL_FUNCTIONS = unreal.GeometryScript_Materials  # script name of UGeometryScriptLibrary_MeshMaterialFunctions
_QUERIES = unreal.GeometryScript_MeshQueries


def _section_stats(asset, section_count):
    """Per section: re-read the mesh, delete every other section, measure what is left (proven helpers only)."""
    stats = {}
    for index in range(section_count):
        mesh = meshes.keep_sections(meshes.read(asset), {index}, section_count)
        tris = meshes.triangle_count(mesh)
        if tris:
            box = meshes.bounds(mesh)
            stats[index] = {"tris": tris, "zmin": box.min.z, "zmax": box.max.z}
        else:
            stats[index] = {"tris": 0, "zmin": 0.0, "zmax": 0.0}
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
        stats = _section_stats(asset, len(slots))
        for index, (name, material) in enumerate(slots):
            entry = stats[index]
            label = ("%s %s" % (name, material.get_name() if material else "")).lower()
            span = (entry["zmax"] - entry["zmin"]) / height if entry["tris"] else 0.0
            skin = any(word in label for word in _SKIN_WORDS) or (span > 0.5 and "cloth" not in label)
            unreal.log("    [%d] %-28s tris=%-6d Z %.1f..%.1f %s" % (
                index, name, entry["tris"], entry["zmin"] if entry["tris"] else 0.0, entry["zmax"] if entry["tris"] else 0.0,
                "SKIN?" if skin else ""))
        _poke(mesh)


def _poke(mesh):
    """Share of garment vertices inside the skin of each candidate body (the lowest share is the authoring body)."""
    bodies = [("SK_tal_nrw_base (Fab authoring)", AUTHORING_BODY)]
    try:
        bodies += [(b.name, b.body_mesh_path) for b in config.load_body_types()]
    except Exception as error:  # noqa: BLE001
        unreal.log_warning("%s body types not read: %s" % (config.LOG_FIT, error))
    for label, path in bodies:
        try:
            unreal.log("    poke vs %-34s %.1f%% inside skin" % (label, 100.0 * clearance.poke_ratio(mesh, path)))
        except Exception as error:  # noqa: BLE001
            unreal.log_warning("    poke vs %s failed: %s" % (label, error))
