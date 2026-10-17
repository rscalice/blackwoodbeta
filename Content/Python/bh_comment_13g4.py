"""
bh_comment_13g4.py  (Phase 13G-4)
Comment boxes for the Inventory detail panel, stats block and context menu:
WBP_Inv_StatRow, WBP_Inv_StatsBlock, WBP_Inv_ItemDetail, WBP_Inv_ContextEntry, WBP_Inv_ContextMenu,
the new Context Menu / detail functions in WBP_Menu_Inventory, the right-click / X / double-click plumbing in
WBP_Inv_BagItem and WBP_Inv_EquipSlot, and the Esc handling in WBP_PlayerMenu.OnPreviewKeyDown.
Defensive and idempotent, same pattern as bh_comment_13g3.py. Missing assets, graphs or pattern matches are logged and skipped.
Run: Tools > Execute Python Script > Content/Python/bh_comment_13g4.py, then read the Output Log for lines tagged [BH_Comment].
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
    INV + "WBP_Inv_StatRow": [
        ("SetRow", "Show a label and a value; the tone picks the value colour from the UI palette (0 bone, 1 green, 2 crimson, 3 muted)", C_VIEW, ALL),
        ("SetRowNumber", "Same as SetRow for a number, shown without decimals", C_VIEW, ALL),
        ("SetRowPair", "Show 'current / max' as whole numbers", C_VIEW, ALL),
    ],
    INV + "WBP_Inv_StatsBlock": [
        ("GetPlayerASC", "Get the local pawn's ability system component", C_LOGIC, ALL),
        ("RefreshProgression", "Level, current XP and XP to next level from the progression component", C_LOGIC, ALL),
        ("RefreshVitals", "Health, Posture and Stamina as current / max from the attribute set", C_LOGIC, ALL),
        ("RefreshCombat", "Attack Power, Defense and Blight Resistance from the attribute set", C_LOGIC, ALL),
        ("RefreshProtection", "Heart-Fragment shielding percent, and Fractured / None from the State.Status.Fracture tag", C_LOGIC, ALL),
        ("RefreshStats", "Refresh every stat row (called on construct and from the inventory 0.5 s poll)", C_LOGIC, ALL),
    ],
    INV + "WBP_Inv_ItemDetail": [
        ("ClearItem", "Nothing selected: hide the details and show the hint", C_VIEW, ALL),
        ("ShowItem", "Show the details for an item, or clear when the item is invalid", C_VIEW, ALL),
        ("FillHeader", "Name, category, icon, description and the EQUIPPED tag", C_VIEW, ALL),
        ("FillBonuses", "List the item's stat bonuses", C_VIEW, ALL),
        ("FillDeltas", "Compare with what is in the target slot and list green / red stat changes (note when it is the equipped item)", C_LOGIC, ALL),
        ("AddStatRow", "Add one coloured label / value row to a list", C_VIEW, ALL),
        ("FormatDeltaText", "Format 'current -> after (+change)'", C_VIEW, ALL),
        ("GetDeltaTone", "Green for gains, crimson for losses, muted for no change", C_VIEW, ALL),
        ("GetCategoryText", "Weapon, Armor, Consumable or Item text for an item", C_VIEW, ALL),
        ("FormatSigned", "Number with an explicit + or - sign", C_VIEW, ALL),
        ("FormatPlain", "Number without decimals", C_VIEW, ALL),
    ],
    INV + "WBP_Inv_ContextEntry": [
        ("SetEntry", "Set the entry id, title and reason line; greyed out when not enabled", C_VIEW, ALL),
        ("NotifyChosen", "Tell the menu which entry was clicked", C_LOGIC, ALL),
        ("EventGraph", "Button clicked: raise the chosen event", C_LOGIC, ["OnClicked", "NotifyChosen"]),
    ],
    INV + "WBP_Inv_ContextMenu": [
        ("BeginMenu", "Start a menu: clear the entries, set the title, show the entry list (not the quantity picker)", C_VIEW, ALL),
        ("AddEntry", "Make one entry button, bind its click, add it to the list", C_VIEW, ALL),
        ("BindEntry", "Listen to one entry button's click", C_LOGIC, ALL),
        ("ShowAt", "Place the menu at the click position, kept inside the page, show it and focus the first entry", C_VIEW, ALL),
        ("CloseMenu", "Hide the menu and quantity picker, tell the inventory it closed", C_VIEW, ALL),
        ("IsOpen", "True while the menu is showing", C_LOGIC, ALL),
        ("OpenQuantityPicker", "Swap the menu for the 'how many' picker (starts at 1)", C_VIEW, ALL),
        ("RefreshPicker", "Show 'chosen / max' and grey the - / + buttons at the limits", C_VIEW, ALL),
        ("ChangeQty", "Add or remove from the chosen quantity, clamped 1 to max", C_LOGIC, ALL),
        ("HandleEntryChosen", "Route a click: quantity buttons change or confirm the amount, anything else is passed on to the inventory", C_LOGIC, ALL),
        ("InitMenu", "Label and bind the six quantity picker buttons", C_VIEW, ALL),
        ("OnMouseButtonDown", "Any click outside the entries closes the menu", C_LOGIC, ALL),
        ("EventGraph", "Page created: set up the picker buttons", C_LOGIC, ["Construct", "InitMenu"]),
    ],
    INV + "WBP_Inv_BagItem": [
        ("OnMouseButtonDown", "Right-click: ask the page for a context menu at the cursor", C_LOGIC, ALL),
        ("OnKeyDown", "Gamepad X / Square: ask the page for a context menu next to this row", C_LOGIC, ALL),
        ("NotifyDoubleClicked", "Raise the double-click event", C_LOGIC, ALL),
        ("EventGraph", "Double-click: raise the event", C_LOGIC, ["DoubleClicked", "NotifyDoubleClicked"]),
    ],
    INV + "WBP_Inv_EquipSlot": [
        ("OnMouseButtonDown", "Right-click: ask the page for a context menu at the cursor", C_LOGIC, ALL),
        ("OnKeyDown", "Gamepad X / Square: ask the page for a context menu next to this slot", C_LOGIC, ALL),
        ("NotifyDoubleClicked", "Raise the double-click event", C_LOGIC, ALL),
        ("EventGraph", "Double-click: raise the event", C_LOGIC, ["DoubleClicked", "NotifyDoubleClicked"]),
    ],
    INV + "WBP_Menu_Inventory": [
        ("UpdateDetailPanel", "Show the selected item in the detail panel: equip slot (what it holds) or bag item (compared with its target slot)", C_VIEW, ALL),
        ("BindBagButton", "Listen to a bag row's context-menu request and double-click", C_LOGIC, ALL),
        ("BindSlotButton", "Listen to an equipment slot's context-menu request and double-click", C_LOGIC, ALL),
        ("BindContextMenu", "Listen to the context menu's chosen action and confirmed quantity", C_LOGIC, ALL),
        ("HandleBagContext", "Right-click / X on a bag row: open the menu", C_LOGIC, ALL),
        ("HandleSlotContext", "Right-click / X on an equipment slot: open the menu", C_LOGIC, ALL),
        ("HandleItemDoubleClick", "Double-click: slot unequips, bag row equips (only if it fits a slot and is not already equipped)", C_LOGIC, ALL),
        ("OpenContextMenuForButton", "Select the clicked row, convert the screen position to page space, then build the slot or bag menu", C_LOGIC, ALL),
        ("BuildBagItemMenu", "Bag menu: category entries, Drop / Drop amount, Inspect", C_VIEW, ALL),
        ("BuildSlotMenu", "Equipment slot menu: Unequip, Inspect", C_VIEW, ALL),
        ("AddCategoryEntries", "Weapon / Armor get Equip or Unequip, consumables get Use and Quick Access", C_VIEW, ALL),
        ("AddEquipEntry", "Equip (greyed when no slot fits) or Unequip when already equipped", C_VIEW, ALL),
        ("AddConsumableEntries", "Use (greyed with the consumable library's refusal reason) and Assign to Quick Access (disabled until 13G-6)", C_VIEW, ALL),
        ("AddDropEntries", "Drop and Drop amount, greyed with the drop rules' reason; Drop amount needs a stack above 1", C_VIEW, ALL),
        ("AddInspectEntry", "Inspect entry", C_VIEW, ALL),
        ("HandleContextAction", "Route the chosen entry to the same server-routed calls the action buttons use", C_LOGIC, ALL),
        ("HandleQuantityConfirmed", "Ask the server to drop the chosen amount", C_LOGIC, ALL),
        ("UseSelectedConsumable", "Use the selected consumable through the same replicated path as the radial wheel", C_LOGIC, ALL),
        ("InspectSelected", "Refresh the detail panel and give it focus", C_VIEW, ALL),
        ("TryCloseContextMenu", "Close the context menu if open and say whether it was (Esc handling asks this first)", C_LOGIC, ALL),
        ("PollRefresh", "Every 0.5 s while visible: refresh equipment state and the character stats", C_LOGIC, ALL),
        ("UpdateActionButtons", "Refresh the detail panel, then update the three action buttons", C_LOGIC, ALL),
    ],
    MENU + "WBP_PlayerMenu": [
        ("OnPreviewKeyDown", "Back / open-menu keys: close the inventory context menu first, otherwise close the player menu; Q / E change tab", C_LOGIC, ALL),
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
    unreal.log("%s 13G-4 DONE: %d comment boxes added" % (TAG, total))


main()
