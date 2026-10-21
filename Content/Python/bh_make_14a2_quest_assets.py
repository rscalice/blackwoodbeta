"""14A-2: create the quest data task assets and fill the quest target table.

Run: Tools > Execute Python Script, or `py bh_make_14a2_quest_assets.py`.
Needs the C++ module built (UNarrativeDataTask and the FBH_QuestTargetRow struct must exist) and the
Quest.Target.Island1.* gameplay tags registered (they are in Config/DefaultGameplayTags.ini).
Safe to re-run: existing data tasks are kept (their fields are re-applied); DT_BH_QuestTargets is created if missing and its rows are
REPLACED by the ROWS list below (edit the list here, not the table, if you want a change to survive a re-run).

1. One UNarrativeDataTask per BH_QuestEvents vocabulary entry: /Game/BlackwoodHollow/Quests/Tasks/DT_Task_<Name>, TaskName exactly the
   constant (UNarrativeTaskManager finds the asset by that text).
2. DT_BH_QuestTargets rows: (TaskName, Argument) of every Q_AmongTheWrack / Q_KingsRoad / Q_GuardiansOfTheSpan task -> Quest.Target tag +
   gate flag. The Argument text MUST equal the Argument typed on the quest task.
"""
import json
import unreal

QUEST_FOLDER = "/Game/BlackwoodHollow/Quests"
TASK_FOLDER = f"{QUEST_FOLDER}/Tasks"
TABLE_NAME = "DT_BH_QuestTargets"
TABLE_PATH = f"{QUEST_FOLDER}/{TABLE_NAME}"
STRUCT_PATH = "/Script/BlackwoodHollowBeta.BH_QuestTargetRow"

lib = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()

# (TaskName constant from BH_QuestEvents.h, description, argument name, default argument)
TASKS = [
    ("OpenContainer", "Open a container or take from a cache.", "ChecklistItemId", ""),
    ("TakeItem", "Take an item into the inventory.", "Item class short name", ""),
    ("WorldFlag", "A world flag becomes set.", "Flag name", ""),
    ("Kill", "Kill an enemy. Argument <identity>, or <identity>.<Rank> for Veteran or higher.", "Enemy identity", ""),
    ("TalkTo", "Talk to an NPC.", "NPC id", ""),
    ("EnterArea", "Enter a quest area (ABH_BeatTrigger::QuestAreaId).", "Area id", ""),
    ("LearnSkill", "Learn a skill.", "Skill id", "OverloadBurst"),
    ("Relight", "Relight a Warden beacon.", "Beacon id (its world flag)", ""),
    ("Attune", "Attune a respawn hub for the first time.", "Hub name", ""),
    ("Interact", "Complete an interaction.", "First actor tag or actor name", ""),
]

G_BARRICADE = "Island1.CoralBarricadeBroken"
G_SPAN = "Island1.SpanGateOpen"
T = "Quest.Target.Island1."

# Placeholder arguments (items do not exist yet): replace them here AND on the quest task when the items are made.
PH_SHIPMENT = "PLACEHOLDER_TradeShipment"
PH_ORDER = "PLACEHOLDER_GatehouseRequisitionOrder"
PH_SPARK = "PLACEHOLDER_DormantWardenSpark"

# (row name, TaskName, Argument, target tag suffix, gate flag, show world marker)
ROWS = [
    # Q_AmongTheWrack
    ("Q1.Scavenge.Cargo", "OpenContainer", "Cargo", "WreckCargo", "", True),
    ("Q1.Scavenge.Sap", "OpenContainer", "Sap", "WreckSap", "", True),
    ("Q1.Scavenge.Cache", "OpenContainer", "Cache", "WreckCache", "", True),
    ("Q1.Barricade", "WorldFlag", G_BARRICADE, "Barricade", "", True),
    ("Q1.Basin.Crabs", "Kill", "Crab", "Basin", G_BARRICADE, True),
    ("Q1.Basin.Veteran", "Kill", "Crab.Veteran", "Basin", G_BARRICADE, True),
    ("Q1.Obelisk", "LearnSkill", "OverloadBurst", "Obelisk", G_BARRICADE, True),
    ("Q1.Choke.Sergeant", "Kill", "CorruptedVanguard.Veteran", "ChokeExit", "", True),
    ("Q1.Choke.Exit", "EnterArea", "Island1.ChokeExit", "ChokeExit", "", True),
    ("Q1.Port", "Attune", "PortVanguard", "Port", "", True),
    # Q_KingsRoad
    ("Q2.Shipments", "TakeItem", PH_SHIPMENT, "TradeShipments", "", True),
    ("Q2.Order", "TakeItem", PH_ORDER, "GatehouseOrder", "", True),
    ("Q2.Spark", "TakeItem", PH_SPARK, "WardenSpark", "", True),
    ("Q2.WardenWatch", "Relight", G_SPAN, "WardenWatch", "", True),
    # Q_GuardiansOfTheSpan
    ("Q3.Gatehouse", "EnterArea", "Island1.Gatehouse", "Gatehouse", "", True),
    ("Q3.Quartermaster", "TalkTo", "Quartermaster", "Quartermaster", "", True),
    ("Q3.Span", "EnterArea", "Island1.Span", "Span", G_SPAN, True),
]


def ensure_task(name, description, arg_name, default_arg):
    path = f"{TASK_FOLDER}/DT_Task_{name}"
    if lib.does_asset_exist(path):
        asset = lib.load_asset(path)
        unreal.log(f"[BH_14A2] exists: {path}")
    else:
        factory = unreal.DataAssetFactory()
        asset = tools.create_asset(f"DT_Task_{name}", TASK_FOLDER, unreal.NarrativeDataTask, factory)
        if asset is None:
            unreal.log_error(f"[BH_14A2] could not create {path} (is unreal.NarrativeDataTask available?)")
            return
        unreal.log(f"[BH_14A2] created: {path}")
    asset.set_editor_property("task_name", name)
    asset.set_editor_property("task_description", description)
    asset.set_editor_property("argument_name", arg_name)
    asset.set_editor_property("task_category", "Blackwood Hollow")
    asset.set_editor_property("default_argument", default_arg)
    lib.save_loaded_asset(asset)


def ensure_table():
    if lib.does_asset_exist(TABLE_PATH):
        return lib.load_asset(TABLE_PATH)
    row_struct = unreal.load_object(None, STRUCT_PATH)
    if row_struct is None:
        unreal.log_error(f"[BH_14A2] row struct not found: {STRUCT_PATH} (build the C++ module first)")
        return None
    factory = unreal.DataTableFactory()
    factory.set_editor_property("struct", row_struct)
    table = tools.create_asset(TABLE_NAME, QUEST_FOLDER, unreal.DataTable, factory)
    unreal.log(f"[BH_14A2] created: {TABLE_PATH}")
    return table


def fill_table(table):
    rows = []
    for row_name, task, arg, tag, gate, marker in ROWS:
        rows.append({
            "Name": row_name,
            "TaskName": task,
            "Argument": arg,
            "TargetTag": {"TagName": T + tag},
            "RequiredGateFlag": gate if gate else "None",
            "bShowWorldMarker": marker,
        })
    problems = unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(rows))
    # Returns the list of import problems (empty = clean) in recent engines; older ones return (bool, list).
    unreal.log(f"[BH_14A2] fill_data_table_from_json_string -> {problems}")
    names = [str(n) for n in unreal.DataTableFunctionLibrary.get_data_table_row_names(table)]
    unreal.log(f"[BH_14A2] {TABLE_NAME} now has {len(names)} rows (expected {len(ROWS)})")
    if len(names) != len(ROWS):
        unreal.log_error("[BH_14A2] row count mismatch: check the Output Log; the Quest.Target tags may not be registered (restart the editor once after pulling Config/DefaultGameplayTags.ini)")
    lib.save_loaded_asset(table)


for task_name, desc, arg_name, default in TASKS:
    ensure_task(task_name, desc, arg_name, default)

tbl = ensure_table()
if tbl is not None:
    fill_table(tbl)

lib.save_directory(QUEST_FOLDER, only_if_is_dirty=False, recursive=True)
unreal.log("[BH_14A2] done: 10 data tasks + DT_BH_QuestTargets rows")
