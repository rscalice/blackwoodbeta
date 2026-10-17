"""Island 1 starter armor placement (idempotent, saves what it changes). Log tag: [BH_StarterArmor].

Open /Game/Levels/WreckageShallows/L_WreckageShallows first (the script loads it if another level is open), then:

    py import importlib, bh_island1_starter_armor as s; importlib.reload(s); s.setup_hint_and_chest()
    (after the C++ rebuild with wave-entry BonusDrops)
    py import importlib, bh_island1_starter_armor as s; importlib.reload(s); s.setup_first_blood_drops()

setup_hint_and_chest(): DA_Hint_ArmorEquip, DT_Loot_StarterArmorWreck (one row granting the Starter Chest AND Pants, Chance 1) and one extra
  guaranteed roll on LootContainer_Cargo_WreckField (Beat 1 wreck cargo).
setup_first_blood_drops(): Enc_Beat2a_Shore_Spawner (one crab) -> BonusDrops Starter Gloves; Enc_Beat2b_Hull_Spawner (the pair) ->
  BonusDrops Starter Boots on the first crab of the entry. Entries are whole-party + once-per-player (the kite shield pattern).
"""
import unreal

TAG = "[BH_StarterArmor]"
LEVEL = "/Game/Levels/WreckageShallows/L_WreckageShallows"
ITEM_DIR = "/Game/BlackwoodHollow/Items/Armor"
HINT_DIR = "/Game/BlackwoodHollow/Data/Hints"
LOOT_DIR = "/Game/BlackwoodHollow/Items/Loot"
OPEN_MENU_ACTION = "/Game/Input/Menu/IA_UI_OpenMenu"
CONTAINER_LABEL = "LootContainer_Cargo_WreckField"
SPAWNER_GLOVES = "Enc_Beat2a_Shore_Spawner"
SPAWNER_BOOTS = "Enc_Beat2b_Hull_Spawner"


def _log(text):
    unreal.log("%s %s" % (TAG, text))


def _warn(text):
    unreal.log_warning("%s %s" % (TAG, text))


def _item_class_path(name):
    return "%s/BI_Armor_Starter_%s.BI_Armor_Starter_%s_C" % (ITEM_DIR, name, name)


def _ensure_level():
    world = unreal.EditorLevelLibrary.get_editor_world()
    if world is None or "L_WreckageShallows" not in world.get_path_name():
        unreal.EditorLevelLibrary.load_level(LEVEL)
        _log("loaded %s" % LEVEL)


def _actor(label):
    for actor in unreal.EditorActorSubsystem().get_all_level_actors():
        if actor.get_actor_label() == label:
            return actor
    raise RuntimeError("actor '%s' not found in the open level" % label)


def _save_all():
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)


def _soft_class(path):
    """A TSoftClassPtr value for the item class at `path` (Python accepts the loaded class for soft class properties)."""
    loaded = unreal.load_class(None, path)
    if loaded is None:
        raise RuntimeError("item class not found: %s" % path)
    return loaded


# --------------------------------------------------------------------------------------------------------------------
# Hint + guaranteed chest loot
# --------------------------------------------------------------------------------------------------------------------

def _manual_trigger():
    """The 'Manual' trigger value, copied from an existing manual hint.

    unreal.BH_HintTrigger is the ABH_HintTrigger actor class (EBH_HintTrigger collides with it in Python), so the enum
    can't be reached by name. The weapon-cache / auto-equip hints are Manual and fire by key, so copy theirs.
    """
    for asset_path in unreal.EditorAssetLibrary.list_assets(HINT_DIR, recursive=True, include_folder=False):
        hint = unreal.load_asset(asset_path)
        if not isinstance(hint, unreal.BH_HintData):
            continue
        if str(hint.get_editor_property("trigger_key")) in ("Item.WeaponCache", "Drop.AutoEquip"):
            return hint.get_editor_property("trigger")
    raise RuntimeError("no existing manual hint (trigger key Item.WeaponCache / Drop.AutoEquip) found in %s" % HINT_DIR)


def _ensure_hint():
    path = "%s/DA_Hint_ArmorEquip" % HINT_DIR
    asset = unreal.load_asset(path)
    if asset is None:
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset("DA_Hint_ArmorEquip", HINT_DIR, unreal.BH_HintData, unreal.DataAssetFactory())
        _log("created %s" % path)
    action = unreal.load_asset(OPEN_MENU_ACTION)
    if action is None:
        _warn("input action %s not found - the hint will have no key glyph" % OPEN_MENU_ACTION)
    asset.set_editor_property("hint_id", "ArmorEquip")
    asset.set_editor_property("title", "New armor")
    asset.set_editor_property("text", "Open the Player Menu and equip it from Inventory.")
    asset.set_editor_property("glyph_actions", [action] if action else [])
    asset.set_editor_property("trigger", _manual_trigger())
    asset.set_editor_property("trigger_key", "Item.Armor")
    asset.set_editor_property("priority", 0)
    asset.set_editor_property("sort_order", 135)
    unreal.EditorAssetLibrary.save_loaded_asset(asset)
    _log("hint saved: %s" % path)


def _ensure_chest_table():
    path = "%s/DT_Loot_StarterArmorWreck" % LOOT_DIR
    table = unreal.load_asset(path)
    if table is None:
        factory = unreal.DataTableFactory()
        factory.set_editor_property("struct", unreal.LootTableRow.static_struct())
        table = unreal.AssetToolsHelpers.get_asset_tools().create_asset("DT_Loot_StarterArmorWreck", LOOT_DIR, unreal.DataTable, factory)
        _log("created %s" % path)
    row = (
        '[{"Name":"StarterChestAndPants","ItemsToGrant":['
        '{"Item":"%s","Quantity":1},{"Item":"%s","Quantity":1}],'
        '"ItemCollectionsToGrant":[],"SubTablesToRoll":[],"Chance":1.0}]'
    ) % (_item_class_path("Chest"), _item_class_path("Pants"))
    result = unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table, row)
    problems = result[1] if isinstance(result, tuple) else []
    if problems:
        _warn("DataTable import messages: %s" % list(problems))
    unreal.EditorAssetLibrary.save_loaded_asset(table)
    _log("%s rows: %s" % (path, list(unreal.DataTableFunctionLibrary.get_data_table_row_names(table))))
    return table


def _add_roll(container, table):
    rolls = list(container.get_editor_property("loot_rolls"))
    for roll in rolls:
        existing = roll.get_editor_property("table_to_roll")
        if existing is not None and existing.get_path_name() == table.get_path_name():
            _log("%s already rolls %s" % (CONTAINER_LABEL, table.get_name()))
            return False
    roll = unreal.LootTableRoll()
    roll.set_editor_property("table_to_roll", table)
    roll.set_editor_property("num_rolls", 1)
    roll.set_editor_property("chance", 1.0)
    rolls.append(roll)
    container.set_editor_property("loot_rolls", rolls)
    container.modify()
    return True


def setup_hint_and_chest():
    _ensure_level()
    _ensure_hint()
    table = _ensure_chest_table()
    container = _actor(CONTAINER_LABEL)
    if _add_roll(container, table):
        _log("%s now has %d loot rolls (Starter Chest + Pants guaranteed)" % (CONTAINER_LABEL, len(container.get_editor_property("loot_rolls"))))
    _save_all()
    _log("done - save the level (File > Save Current Level) if the editor still shows it dirty")


# --------------------------------------------------------------------------------------------------------------------
# First-blood crab drops
# --------------------------------------------------------------------------------------------------------------------

def _drop_entry(item_name):
    entry = unreal.BH_DropEntry()
    entry.set_editor_property("item_class", _soft_class(_item_class_path(item_name)))
    entry.set_editor_property("chance", 1.0)
    entry.set_editor_property("min_quantity", 1)
    entry.set_editor_property("max_quantity", 1)
    entry.set_editor_property("grant_to_whole_party", True)
    entry.set_editor_property("once_per_player", True)
    return entry


def _set_bonus(spawner_label, item_name):
    spawner = _actor(spawner_label)
    waves = list(spawner.get_editor_property("waves"))
    if not waves:
        raise RuntimeError("%s has no waves" % spawner_label)
    wave = waves[0]
    entries = list(wave.get_editor_property("entries"))
    if not entries:
        raise RuntimeError("%s wave 1 has no entries" % spawner_label)
    entry = entries[0]
    entry.set_editor_property("bonus_drops", [_drop_entry(item_name)])
    entry.set_editor_property("bonus_drop_count", 1)
    entries[0] = entry
    wave.set_editor_property("entries", entries)
    waves[0] = wave
    spawner.set_editor_property("waves", waves)
    spawner.modify()
    _log("%s: first spawn of wave 1 / entry 1 (%s x%s) drops Starter %s" % (
        spawner_label, entry.get_editor_property("enemy_class").get_name() if entry.get_editor_property("enemy_class") else "?",
        entry.get_editor_property("spawn_count"), item_name))


def setup_first_blood_drops():
    _ensure_level()
    _set_bonus(SPAWNER_GLOVES, "Gloves")
    _set_bonus(SPAWNER_BOOTS, "Boots")
    _save_all()
    _log("done - save the level (File > Save Current Level) if the editor still shows it dirty")
