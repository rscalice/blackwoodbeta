"""
bh_comment_13g6.py  (Phase 13G-6)
Comment boxes for the Quick Access picker (WBP_Inv_QuickAccessPicker) and its hooks in WBP_Menu_Inventory.
Defensive and idempotent, same pattern as bh_comment_13g5.py.
Run: Tools > Execute Python Script > Content/Python/bh_comment_13g6.py, then read the Output Log for lines tagged [BH_Comment].
"""
import unreal

TAG = "[BH_Comment]"
LIB = unreal.BH_BlueprintCommentLibrary
C_LOGIC = unreal.LinearColor(0.12, 0.35, 0.18, 0.45)
C_VIEW = unreal.LinearColor(0.35, 0.12, 0.40, 0.45)
C_INPUT = unreal.LinearColor(0.15, 0.25, 0.45, 0.45)
ALL = ["*"]

INV = "/Game/BlackwoodHollow/UI/Menu/Inventory/"

BOXES = {
    INV + "WBP_Inv_QuickAccessPicker": [
        ("FindRadial", "Find the local player's Quick Access radial component (never replicated, local only)", C_LOGIC, ALL),
        ("InitLists", "Collect the six slot buttons, six Clear buttons and six icons into arrays", C_VIEW, ALL),
        ("BindEntry", "Listen for a click or confirm on one picker button", C_INPUT, ALL),
        ("RefreshSlots", "Show what each of the six slots holds now: name, count, icon and a Clear button when filled", C_VIEW, ALL),
        ("OpenFor", "Remember the item being assigned, refresh the six slots, show the picker and focus the first slot", C_LOGIC, ALL),
        ("ClosePicker", "Hide the picker", C_LOGIC, ALL),
        ("HandleChosen", "A slot button (s0..s5) assigns, a Clear button (c0..c5) clears", C_INPUT, ALL),
        ("AssignToSlot", "Close, clear any other slot holding the same item (move, never duplicate), set the chosen slot, refresh the wheel data", C_LOGIC, ALL),
        ("ClearSlot", "Close and empty the chosen slot", C_LOGIC, ALL),
        ("IsOpen", "Report whether the picker is showing", C_LOGIC, ALL),
        ("OnMouseButtonDown", "A click outside the buttons closes the picker", C_INPUT, ALL),
        ("EventGraph", "On construct: gather the buttons, bind them and start hidden", C_LOGIC, ALL),
    ],
    INV + "WBP_Menu_Inventory": [
        ("AddQuickAccessEntry", "Context menu entry: enabled for consumables, greyed with a reason for everything else", C_VIEW, ALL),
        ("OpenQuickAccessPicker", "Open the Quick Access picker for the selected item", C_LOGIC, ALL),
        ("TryCloseContextMenu", "Esc / B: close the picker first, otherwise the context menu; report whether anything was closed", C_INPUT, ALL),
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
    unreal.log("%s 13G-6 DONE: %d comment boxes added" % (TAG, total))


main()
