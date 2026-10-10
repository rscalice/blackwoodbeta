"""MetaHuman Character operations: edit session, constraints, rigging state, outfits and builds."""
import unreal

from . import assets, checks, config

OUTFIT_SLOT = "Outfits"


def subsystem():
    return unreal.get_editor_subsystem(unreal.MetaHumanCharacterEditorSubsystem)


def open_for_edit(mhc):
    """Register the character with the subsystem (can take a minute; returns False if already open)."""
    return subsystem().try_add_object_to_edit(mhc)


def close_edit(mhc):
    subsystem().remove_object_to_edit(mhc)


def apply_constraints(mhc, targets):
    """Set the target measurements; returns True when anything changed and was committed.

    Constraints not named in `targets` keep their current activation and value.
    """
    ss = subsystem()
    current = ss.get_body_constraints(mhc)
    changed = False
    updated = []
    for constraint in current:
        name = str(constraint.name)
        target = targets.get(name)
        is_active = constraint.is_active
        measurement = constraint.target_measurement
        if target is not None and (not is_active or abs(measurement - target) > 1e-3):
            is_active, measurement, changed = True, float(target), True
        updated.append(
            unreal.MetaHumanCharacterBodyConstraint(
                name=constraint.name,
                is_active=is_active,
                target_measurement=measurement,
                min_measurement=constraint.min_measurement,
                max_measurement=constraint.max_measurement,
            )
        )
    unknown = set(targets) - {str(c.name) for c in current}
    if unknown:
        raise RuntimeError("Unknown body constraint(s): %s" % sorted(unknown))
    if changed:
        ss.set_body_constraints(mhc, updated)
        ss.commit_body_state(mhc)
    return changed


def start_auto_rig(mhc):
    """Start the asynchronous auto-rig; poll can_build() until it turns True."""
    params = unreal.MetaHumanCharacterAutoRiggingRequestParams()
    params.set_editor_property("rig_type", unreal.MetaHumanRigType.JOINTS_AND_BLEND_SHAPES)
    params.set_editor_property("blocking", False)
    params.set_editor_property("report_progress", True)
    subsystem().request_auto_rigging(mhc, params)


def can_build(mhc):
    return subsystem().can_build_meta_human(mhc)


def _instance(mhc):
    collection = mhc.get_editor_property("internal_collection")
    return collection, collection.get_editor_property("default_instance")


def clear_outfits(mhc):
    """Deselect every outfit so the next build contains only the bare body."""
    _, instance = _instance(mhc)
    instance.set_single_slot_selection(OUTFIT_SLOT, unreal.MetaHumanPaletteItemKey())
    mhc.modify(True)


def select_outfits(mhc, wardrobe_item_packages):
    """Make exactly these wardrobe items the selected outfits (add to the collection if needed)."""
    collection, instance = _instance(mhc)
    clear_outfits(mhc)
    for package in wardrobe_item_packages:
        item = assets.load(package)
        keys = collection.get_item_keys_for_wardrobe_item(item)
        key = keys[0] if keys else collection.try_add_item_from_wardrobe_item(OUTFIT_SLOT, item)
        selection = unreal.MetaHumanPipelineSlotSelection(slot_name=OUTFIT_SLOT, selected_item=key)
        if not instance.try_add_slot_selection(selection):
            raise RuntimeError("Could not select outfit " + package)
    mhc.modify(True)


def build(mhc, build_root):
    """Synchronous build (Optimized pipeline, Medium quality) into `build_root`."""
    params = unreal.MetaHumanCharacterEditorBuildParameters()
    params.absolute_build_path = build_root
    params.common_folder_path = config.COMMON_FOLDER
    params.pipeline_type = unreal.MetaHumanDefaultPipelineType.OPTIMIZED
    params.pipeline_quality = unreal.MetaHumanQualityLevel.MEDIUM
    subsystem().build_meta_human(mhc, params)
    checks.forbid_metahumans_folder()
