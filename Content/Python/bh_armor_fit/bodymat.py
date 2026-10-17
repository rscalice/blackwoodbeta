"""Masked body skin material for the built body types.

The MetaHuman build writes <body>/Body/Materials/MI_Body_Baked_VT (parent: a MetaHuman Common instance of
M_skin_unified_baked, which is Opaque). Armor body masking needs the skin to be a Masked material with the
BH_Mask_* texture parameters, so every built body gets a project-side copy of that instance re-parented onto
/Game/BlackwoodHollow/Characters/Materials/M_BH_Skin_Masked (a copy of M_skin_unified_baked with an opacity mask =
1 - max(BH_Mask_Helm/Chest/Gloves/Pants/Boots), all defaulting to black = nothing hidden) and assigned to the body
mesh's material slot. Nothing under /Game/MetaHumans/Common is touched.

body_types._stage_build calls apply() after every body build (a rebuild resets the slot to the built instance);
apply_all() does it for the existing bodies.
"""
import unreal

from . import assets, config
from . import report as rpt

MATERIALS_ROOT = "/Game/BlackwoodHollow/Characters/Materials"
MASKED_PARENT = MATERIALS_ROOT + "/MI_BH_Skin_Baked_VT"  # copy of the MetaHuman Common intermediate instance, parented to M_BH_Skin_Masked


def apply(body):
    """Make `body`'s body mesh use a masked skin instance; returns the instance package ('' when nothing to do)."""
    mesh = assets.load(body.body_mesh_path)
    slot = mesh.materials[0]
    current = slot.material_interface
    if current is None:
        raise RuntimeError("body mesh %s has no material in slot 0" % body.body_mesh_path)
    if current.get_path_name().startswith(MATERIALS_ROOT + "/"):
        return ""  # already ours
    if not assets.exists(MASKED_PARENT):
        raise RuntimeError("missing %s (the masked skin parent chain)" % MASKED_PARENT)
    package = "%s/MI_BH_Body_Baked_VT_%s" % (MATERIALS_ROOT, body.name)
    if not assets.exists(package):
        assets.duplicate(current.get_path_name().split(".")[0], package)
    instance = assets.load(package)
    unreal.MaterialEditingLibrary.set_material_instance_parent(instance, assets.load(MASKED_PARENT))
    mesh.modify(True)
    materials = list(mesh.materials)
    slot = materials[0]
    slot.set_editor_property("material_interface", instance)
    materials[0] = slot
    mesh.set_editor_property("materials", materials)
    assets.save([package, body.body_mesh_path])
    rpt.log(config.LOG_FIT, "%s: masked skin material %s assigned" % (body.name, package))
    return package


def apply_all():
    """Masked skin for every body type that does not have it yet."""
    for body in config.load_body_types():
        if assets.exists(body.body_mesh_path):
            apply(body)
