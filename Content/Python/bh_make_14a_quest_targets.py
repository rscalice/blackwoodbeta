"""14A-1: create the (empty) quest target table DT_BH_QuestTargets with the FBH_QuestTargetRow struct.
Run: Tools > Execute Python Script, or `py bh_make_14a_quest_targets.py`. Needs the C++ module built first (the row struct must exist).
Safe to re-run: an existing table is left untouched (rows are authored by hand in the editor)."""
import unreal

FOLDER = "/Game/BlackwoodHollow/Quests"
NAME = "DT_BH_QuestTargets"
PATH = f"{FOLDER}/{NAME}"
STRUCT_PATH = "/Script/BlackwoodHollowBeta.BH_QuestTargetRow"

lib = unreal.EditorAssetLibrary

if lib.does_asset_exist(PATH):
    unreal.log(f"[BH_14A] exists: {PATH}")
else:
    row_struct = unreal.load_object(None, STRUCT_PATH)
    if row_struct is None:
        unreal.log_error(f"[BH_14A] row struct not found: {STRUCT_PATH} (build the C++ module first)")
    else:
        factory = unreal.DataTableFactory()
        factory.set_editor_property("struct", row_struct)
        table = unreal.AssetToolsHelpers.get_asset_tools().create_asset(NAME, FOLDER, unreal.DataTable, factory)
        if table is None:
            unreal.log_error(f"[BH_14A] could not create {PATH}")
        else:
            lib.save_directory(FOLDER, only_if_is_dirty=False, recursive=False)
            unreal.log(f"[BH_14A] created: {PATH} (empty; add rows: TaskName, Argument, TargetTag, ...)")
