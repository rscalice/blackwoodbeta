"""Island 1 beat 1 "Scavenge the wreck" as a three-item checklist (idempotent, saves what it changes). Log tag: [BH_Scavenge].

Needs the C++ rebuild with ABH_BeatTarget::ChecklistItemId / ChecklistItemId on the containers and the cache. Open
/Game/Levels/WreckageShallows/L_WreckageShallows (the script loads it if another level is open), then:

    py import importlib, bh_island1_scavenge as s; importlib.reload(s); s.setup()

What it does:
  * ChecklistItemId on the weapon cache ("Cache"), LootContainer_Cargo_WreckField ("Cargo") and LootContainer_Sap_WreckField ("Sap").
  * One ABH_BeatTarget per item with BeatTag Beat.Island1.WreckCache and the same ChecklistItemId: the existing BeatTarget_01_WreckCache is
    moved onto the cache; BeatTarget_01_WreckCargo / BeatTarget_01_WreckSap are created above the two containers (not spatially loaded).
  * BeatTrigger_01_WreckCache no longer completes the beat (its BeatToComplete is cleared): the beat now ends when one player has done all three.
  * DT_BH_Beats row 01_WreckCache: ObjectiveText "Scavenge the wreck" (the tracker appends " (n/3)" itself).
"""
import unreal

TAG = "[BH_Scavenge]"
LEVEL = "/Game/Levels/WreckageShallows/L_WreckageShallows"
BEATS_TABLE = "/Game/BlackwoodHollow/Data/DT_BH_Beats"
OLD_TEXT = "Search the wreck for a weapon"
NEW_TEXT = "Scavenge the wreck"
MARKER_LIFT_CM = 120.0

CACHE_ID = "Cache"
ITEMS = (  # (label of the container actor, checklist id, new target label)
    ("LootContainer_Cargo_WreckField", "Cargo", "BeatTarget_01_WreckCargo"),
    ("LootContainer_Sap_WreckField", "Sap", "BeatTarget_01_WreckSap"),
)
EXISTING_TARGET = "BeatTarget_01_WreckCache"
EXISTING_TRIGGER = "BeatTrigger_01_WreckCache"


def _log(text):
    unreal.log("%s %s" % (TAG, text))


def _warn(text):
    unreal.log_warning("%s %s" % (TAG, text))


def _ensure_level():
    world = unreal.EditorLevelLibrary.get_editor_world()
    if world is None or "L_WreckageShallows" not in world.get_path_name():
        unreal.EditorLevelLibrary.load_level(LEVEL)
        _log("loaded %s" % LEVEL)


def _actors():
    return list(unreal.EditorActorSubsystem().get_all_level_actors())


def _by_label(label):
    for actor in _actors():
        if actor.get_actor_label() == label:
            return actor
    raise RuntimeError("actor '%s' not found in the open level" % label)


def _weapon_cache():
    found = [a for a in _actors() if isinstance(a, unreal.BH_WeaponCache)]
    if not found:
        raise RuntimeError("no weapon cache (BH_WeaponCache) in the open level")
    if len(found) > 1:
        _warn("%d weapon caches found; using '%s'" % (len(found), found[0].get_actor_label()))
    return found[0]


def _target(label, template, location, item_id):
    """Find or create a BH_BeatTarget with this label; sets tag, checklist id and location."""
    target = None
    for actor in _actors():
        if actor.get_actor_label() == label:
            target = actor
            break
    if target is None:
        target = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.BH_BeatTarget, location)
        target.set_actor_label(label)
        _log("created %s" % label)
    target.set_editor_property("beat_tag", template.get_editor_property("beat_tag"))
    target.set_editor_property("checklist_item_id", item_id)
    target.set_actor_location(location, False, False)
    return target


def _update_beat_text():
    table = unreal.load_asset(BEATS_TABLE)
    if table is None:
        _warn("%s not found - set the ObjectiveText of row 01_WreckCache to '%s' by hand" % (BEATS_TABLE, NEW_TEXT))
        return
    try:
        text = unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table)
        if isinstance(text, tuple):
            text = text[-1]
        if NEW_TEXT in text and OLD_TEXT not in text:
            _log("beat text already '%s'" % NEW_TEXT)
            return
        if OLD_TEXT not in text:
            _warn("row text '%s' not found in the exported table; edit row 01_WreckCache by hand" % OLD_TEXT)
            return
        result = unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table, text.replace(OLD_TEXT, NEW_TEXT))
        problems = result[1] if isinstance(result, tuple) else []
        if problems:
            _warn("DataTable import messages: %s" % list(problems))
        unreal.EditorAssetLibrary.save_loaded_asset(table)
        _log("beat text -> '%s'" % NEW_TEXT)
    except Exception as error:  # noqa: BLE001
        _warn("could not rewrite %s (%s): set the ObjectiveText of row 01_WreckCache to '%s' by hand" % (BEATS_TABLE, error, NEW_TEXT))


def setup():
    _ensure_level()
    cache = _weapon_cache()
    template = _by_label(EXISTING_TARGET)

    cache.set_editor_property("checklist_item_id", CACHE_ID)
    cache_at = cache.get_actor_location() + unreal.Vector(0.0, 0.0, MARKER_LIFT_CM)
    _target(EXISTING_TARGET, template, cache_at, CACHE_ID)
    _log("cache '%s' -> id '%s'" % (cache.get_actor_label(), CACHE_ID))

    for container_label, item_id, target_label in ITEMS:
        container = _by_label(container_label)
        container.set_editor_property("checklist_item_id", item_id)
        _target(target_label, template, container.get_actor_location() + unreal.Vector(0.0, 0.0, MARKER_LIFT_CM), item_id)
        _log("%s -> id '%s', marker %s" % (container_label, item_id, target_label))

    try:
        trigger = _by_label(EXISTING_TRIGGER)
        trigger.set_editor_property("beat_to_complete", unreal.GameplayTag())
        _log("%s cleared (the checklist completes the beat now)" % EXISTING_TRIGGER)
    except RuntimeError as error:
        _warn(str(error))

    _update_beat_text()
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    _log("done - save the level (File > Save Current Level) if the editor still shows it dirty")
