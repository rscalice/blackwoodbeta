"""
bh_comment_13g3.py  (Phase 13G-3b)
Comment boxes for the Inventory page work: WBP_Inv_EquipSlot, WBP_Inv_BagItem, WBP_Menu_Inventory,
plus the Part A graphs WBP_UI_ListButton (compact mode) and WBP_Menu_Journal.RebuildNotes.
Defensive and idempotent, same pattern as bh_comment_13g.py. Missing assets, graphs or pattern matches are logged and skipped.
Run: Tools > Execute Python Script > Content/Python/bh_comment_13g3.py, then read the Output Log for lines tagged [BH_Comment].
"""
import unreal

TAG = "[BH_Comment]"
LIB = unreal.BH_BlueprintCommentLibrary
C_LOGIC = unreal.LinearColor(0.12, 0.35, 0.18, 0.45)
C_VIEW = unreal.LinearColor(0.35, 0.12, 0.40, 0.45)
ALL = ["*"]

MENU = "/Game/BlackwoodHollow/UI/Menu/"
INV = MENU + "Inventory/"

BOXES = {
    INV + "WBP_Inv_EquipSlot": [
        ("IsSlotUnlocked", "Ask the player state whether this slot is usable (set B slots are locked on Island 1); true if no player state yet", C_LOGIC, ALL),
        ("ShowLocked", "Locked slot: disabled, dimmed, shows 'Unlocks on Island 2'", C_VIEW, ALL),
        ("ShowItem", "Unlocked slot: show the item name and icon, or a dimmed 'Empty'", C_VIEW, ALL),
        ("Refresh", "Set the slot caption from EquipSlot, then show the item or the locked state", C_VIEW, ALL),
    ],
    INV + "WBP_Inv_BagItem": [
        ("Refresh", "Show name, icon, 'xN' quantity (only above 1) and the EQUIPPED caption", C_VIEW, ALL),
        ("SetItem", "Store the item this row shows, then refresh", C_VIEW, ALL),
    ],
    INV + "WBP_Menu_Inventory": [
        ("GetLocalPlayerState", "Get this player's BH_PlayerState from the owning controller", C_LOGIC, ALL),
        ("GetEquippedWeapon", "Weapon slots: read the item from the pawn's loadout component", C_LOGIC, ALL),
        ("GetEquippedArmor", "Armor slots: the active armor item in the bag whose armor slot matches", C_LOGIC, ALL),
        ("GetEquippedItemForSlot", "Item equipped in a slot: weapons from the loadout, armor from the bag", C_LOGIC, ALL),
        ("AddEquipSlot", "Track one equipment slot widget and add it to the selection group", C_LOGIC, ALL),
        ("RegisterEquipSlots", "Register the 9 equipment slots in EBH_EquipSlot order", C_LOGIC, ALL),
        ("InitSelectionGroup", "Create the single selection group (nothing required) and register the equipment slots", C_LOGIC, ALL),
        ("RebuildEquipSlots", "Refresh every equipment slot with what is equipped in it", C_VIEW, ALL),
        ("UpdateBagUsage", "Show bag slots used / capacity and weight used / capacity", C_VIEW, ALL),
        ("AddBagItemButton", "Make one bag row for an item, add it to the list and the selection group", C_VIEW, ALL),
        ("RebuildBag", "Rebuild the bag list from the inventory, show the empty line, update usage, keep the selection", C_VIEW, ALL),
        ("RestoreBagSelection", "Reselect the previously selected item if it is still in the bag, else clear the selection", C_LOGIC, ALL),
        ("SyncSelectedSlotItem", "If an equipment slot is selected, keep SelectedItem equal to what is in that slot", C_LOGIC, ALL),
        ("RefreshEquipState", "Light refresh (no bag rebuild): equipment slots, EQUIPPED captions, selection, buttons", C_LOGIC, ALL),
        ("RefreshAll", "Full refresh: rebuild the bag, then the equipment state", C_LOGIC, ALL),
        ("PollRefresh", "Every 0.5 s while visible: refresh equipment state (no equip-change delegate exists)", C_LOGIC, ALL),
        ("IsItemCompatibleWithSlot", "Client-side mirror of the equip rules: armor slot match, weapon grip vs main/off-hand slot", C_LOGIC, ALL),
        ("IsSlotEmpty", "True when nothing is equipped in the slot", C_LOGIC, ALL),
        ("FindSlotMatching", "Find the first slot that is unlocked, compatible and (optionally) empty", C_LOGIC, ALL),
        ("FindTargetSlotForItem", "Pick the slot an item would equip to: an empty compatible slot first, else any compatible slot", C_LOGIC, ALL),
        ("FindEquippedSlotOfItem", "Find which slot currently holds the item", C_LOGIC, ALL),
        ("HandleSelectionChanged", "Selection changed: equipment slot (remember slot and its item) or bag row (remember item), then update buttons", C_LOGIC, ALL),
        ("ApplyActionStates", "Enable Equip / Unequip / Drop from the selection, equip state, target slot and drop rules", C_LOGIC, ALL),
        ("UpdateActionButtons", "Update the three action buttons for the current selection", C_LOGIC, ALL),
        ("SetupActionButtons", "Label the Equip, Unequip and Drop buttons", C_VIEW, ALL),
        ("HandleActionButtonClicked", "One handler for all three buttons: route by which button was clicked", C_LOGIC, ALL),
        ("OnEquipClicked", "Ask the server (player menu component) to equip the selected item into its target slot", C_LOGIC, ALL),
        ("OnUnequipClicked", "Ask the server to empty the selected slot (or the slot holding the selected item)", C_LOGIC, ALL),
        ("OnDropClicked", "Ask the server to drop the whole stack of the selected item", C_LOGIC, ALL),
        ("HandleActionResult", "Server answered: failure shows the reason for 3 s, success hides it and refreshes", C_LOGIC, ALL),
        ("HideActionResult", "Hide the action result line", C_VIEW, ALL),
        ("EventGraph", "Page created: wire the three action buttons once (all route to one handler)", C_LOGIC,
         ["OnInitialized", "AssignOnClicked", "GetBtn_", "OnButtonBaseClicked", "HandleActionButtonClicked"]),
        ("EventGraph", "Page shown: set up buttons and the selection group, follow selection, action results and inventory changes, poll every 0.5 s, refresh once", C_LOGIC,
         ["EventConstruct", "SetupActionButtons", "InitSelectionGroup", "AssignOnSelectedButtonBaseChanged", "GetLocalPlayerState",
          "GetPlayerMenu", "AssignOnActionResult", "GetInventory", "AssignOnInventoryUpdated", "SetTimer", "RefreshAll"]),
        ("EventGraph", "Selection group fired: handle the new selection", C_LOGIC, ["OnSelectedButtonBaseChanged_Event", "HandleSelectionChanged"]),
        ("EventGraph", "Server action result arrived: show or hide the message", C_LOGIC, ["OnActionResult_Event", "HandleActionResult"]),
        ("EventGraph", "Bag contents changed: full refresh", C_LOGIC, ["OnInventoryUpdated_Event"]),
        ("EventGraph", "Page hidden: stop the poll and message timers", C_LOGIC, ["EventDestruct", "ClearTimer"]),
    ],
    MENU + "WBP_UI_ListButton": [
        ("ApplyCompact", "Compact mode: shrink the button height and padding, hide the sub-line, use the caption font", C_VIEW, ALL),
        ("SetCompactMode", "Set compact mode on or off and re-apply the look", C_VIEW, ALL),
        ("EventGraph", "Designer preview: apply compact mode so the editor matches the game", C_VIEW, ["PreConstruct", "ApplyCompact"]),
    ],
    MENU + "WBP_Menu_Journal": [
        ("RebuildNotes", "Rebuild the Field Notes list from the hint subsystem and select the first note", C_VIEW, ALL),
    ],
}


def norm(s):
    return "".join(str(s).lower().split())


def parse(lines):
    out = []
    for line in lines:
        parts = [p.strip() for p in str(line).split("|")]
        if len(parts) >= 3:
            out.append((parts[0], parts[1], parts[3] if len(parts) > 3 else ""))
    return out


def process(path, boxes):
    added = 0
    bp = unreal.load_asset(path)
    if not bp:
        unreal.log_warning("%s MISSING asset %s (skipped %d boxes)" % (TAG, path, len(boxes)))
        return 0
    short = path.split("/")[-1]
    for graph, text, colour, patterns in boxes:
        try:
            nodes = parse(LIB.list_graph_nodes_ex(bp, graph))
        except Exception as e:
            unreal.log_warning("%s %s/%s: cannot list nodes (%s)" % (TAG, short, graph, e))
            continue
        if not nodes:
            unreal.log_warning("%s %s/%s: graph missing or empty, skipped '%s'" % (TAG, short, graph, text[:40]))
            continue
        if any(n.startswith("EdGraphNode_Comment") and norm(text) in norm(t) for n, t, _ in nodes):
            unreal.log("%s %s/%s: already exists '%s'" % (TAG, short, graph, text[:40]))
            continue
        real = [(n, t, m) for n, t, m in nodes if not n.startswith("EdGraphNode_Comment")]
        if patterns == ALL:
            hits = [n for n, t, m in real]
        else:
            pats = [norm(p) for p in patterns]
            hits = [n for n, t, m in real if any(p in norm(t) or (m and p in norm(m)) for p in pats)]
        if not hits:
            unreal.log_warning("%s %s/%s: no nodes matched for '%s'" % (TAG, short, graph, text[:40]))
            continue
        try:
            ok = LIB.add_comment_box(bp, graph, hits, text, colour, 40)
        except Exception as e:
            unreal.log_warning("%s %s/%s: add_comment_box failed (%s)" % (TAG, short, graph, e))
            continue
        if ok:
            added += 1
            unreal.log("%s %s/%s: ADDED box over %d nodes: %s" % (TAG, short, graph, len(hits), text[:60]))
        else:
            unreal.log_warning("%s %s/%s: add_comment_box returned false for '%s'" % (TAG, short, graph, text[:40]))
    try:
        LIB.compile_blueprint(bp)
        LIB.save_blueprint(bp)
    except Exception as e:
        unreal.log_warning("%s %s: compile or save failed (%s)" % (TAG, short, e))
    return added


def main():
    total = 0
    for path, boxes in BOXES.items():
        try:
            total += process(path, boxes)
        except Exception as e:
            unreal.log_warning("%s %s: unexpected error (%s)" % (TAG, path, e))
    unreal.log("%s 13G-3b DONE: %d comment boxes added" % (TAG, total))


main()
