"""
bh_comment_13g7.py  (Phase 13G-7)
Comment boxes for the Heart-Fragment tab (WBP_Menu_Heart). Defensive and idempotent, same pattern as bh_comment_13g6.py.
Run: Tools > Execute Python Script > Content/Python/bh_comment_13g7.py, then read the Output Log for lines tagged [BH_Comment].
"""
import unreal

TAG = "[BH_Comment]"
LIB = unreal.BH_BlueprintCommentLibrary
C_LOGIC = unreal.LinearColor(0.12, 0.35, 0.18, 0.45)
C_VIEW = unreal.LinearColor(0.35, 0.12, 0.40, 0.45)
C_INPUT = unreal.LinearColor(0.15, 0.25, 0.45, 0.45)
ALL = ["*"]

MENU = "/Game/BlackwoodHollow/UI/Menu/"

BOXES = {
    MENU + "WBP_Menu_Heart": [
        ("GetLocalPlayerState", "Find this player's own PlayerState (fragment slots and the learned flag live there)", C_LOGIC, ALL),
        ("IsSlotLocked", "A slot is locked when nothing is equipped and it is slot 2 or 3, or slot 1 before Overload Burst is learned", C_LOGIC, ALL),
        ("GetSlotFragmentName", "Read the equipped fragment's name from its class defaults", C_LOGIC, ALL),
        ("GetSlotCooldownDuration", "Read the equipped fragment's cooldown from its class defaults (never hard-coded)", C_LOGIC, ALL),
        ("GetFragmentDescription", "Description text per slot", C_VIEW, ALL),
        ("BuildSlotButtons", "Create the three selectable slot rows once and register them in the selection group", C_VIEW, ALL),
        ("RefreshSlotRows", "Label each row: fragment name, Locked or Empty, plus its key number", C_VIEW, ALL),
        ("RefreshDetail", "Fill the detail panel for the selected slot: name, description, cooldown and key glyph, or the locked reason", C_VIEW, ALL),
        ("HideAllGlyphs", "Hide the three key glyphs before showing the right one", C_VIEW, ALL),
        ("RefreshShielding", "Show Fragment Shielding as a percentage", C_VIEW, ALL),
        ("RefreshAll", "Refresh rows, detail and shielding", C_LOGIC, ALL),
        ("HandleSlotSelected", "A row was selected by mouse or gamepad: remember it and show its detail", C_LOGIC, ALL),
        ("EventGraph", "On construct: build the rows, listen for fragment changes, refresh twice a second and select slot 1", C_LOGIC, ALL),
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
    unreal.log("%s 13G-7 DONE: %d comment boxes added" % (TAG, total))


main()
