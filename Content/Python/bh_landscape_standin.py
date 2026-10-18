"""Island 1 landscape stand-in: use STF Landscape Pro as-is.  Run in the Output Log:

    py bh_landscape_standin.py

Reparents MI_BH_Landscape_Island1 to STF M_landscape_pro_v2_no_tesselation (keeps STF's own
grass/foliage output), clears the orphaned old overrides, assigns it as the Landscape Material
on every Landscape in L_WreckageShallows, and saves explicit paths.  Idempotent.
"""
import unreal

TAG = "[BH_Land]"
STF = "/Game/Fab/STF/Pack03-LandscapePro/Environment/Landscape/Landscape/v2/M_landscape_pro_v2_no_tesselation"
MI_DIR = "/Game/Levels/WreckageShallows/Materials"
MI = MI_DIR + "/MI_BH_Landscape_Island1"
LEVEL = "/Game/Levels/WreckageShallows/L_WreckageShallows"

mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary


def log(m):
    unreal.log("%s %s" % (TAG, m))


def main():
    parent = eal.load_asset(STF)
    if not parent:
        unreal.log_error("%s STF material not found: %s" % (TAG, STF))
        return
    if eal.does_asset_exist(MI):
        mi = eal.load_asset(MI)
    else:
        mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "MI_BH_Landscape_Island1", MI_DIR, unreal.MaterialInstanceConstant,
            unreal.MaterialInstanceConstantFactoryNew())
        log("created MI")
    mel.set_material_instance_parent(mi, parent)
    mel.clear_all_material_instance_parameters(mi)  # drop orphaned Tropical overrides
    for n in range(1, 8):  # dry beach: no puddle sheen
        mel.set_material_instance_scalar_parameter_value(mi, "Puddle Strength Layer_%d" % n, 0.0)
    mel.update_material_instance(mi)
    eal.save_asset(MI, only_if_is_dirty=False)
    log("MI reparented to STF v2 no-tessellation and saved")

    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    lands = [a for a in sub.get_all_level_actors() if isinstance(a, unreal.LandscapeProxy)]
    if not lands:
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(LEVEL)
        lands = [a for a in sub.get_all_level_actors() if isinstance(a, unreal.LandscapeProxy)]
    if not lands:
        unreal.log_warning("%s no Landscape found; open %s and rerun" % (TAG, LEVEL))
        return
    for a in lands:
        a.set_editor_property("landscape_material", mi)
        log("landscape_material set on %s" % a.get_actor_label())
    unreal.EditorLoadingAndSavingUtils.save_packages([a.get_outermost() for a in lands], False)
    log("done")


main()
