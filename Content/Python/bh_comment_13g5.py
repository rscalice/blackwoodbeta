"""
bh_comment_13g5.py  (Phase 13G-5)
Comment boxes for the live character preview (WBP_Inv_Preview) and its hooks in WBP_Menu_Inventory / WBP_PlayerMenu,
plus RefreshDetailIfChanged (left over from 13G-4). Defensive and idempotent, same pattern as bh_comment_13g4.py.
Run: Tools > Execute Python Script > Content/Python/bh_comment_13g5.py, then read the Output Log for lines tagged [BH_Comment].
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
    INV + "WBP_Inv_Preview": [
        ("MakeMID", "Turn the preview image's material into a dynamic instance and keep it", C_VIEW, ALL),
        ("EnsurePreview", "Spawn the local-only preview actor for the owning pawn once, then feed its render target to the material", C_LOGIC, ALL),
        ("SyncPreview", "Page shown: make sure the preview exists and renders. Page hidden: stop rendering", C_LOGIC, ALL),
        ("ClosePreview", "Menu closed: destroy the preview actor (never more than one)", C_LOGIC, ALL),
        ("RefreshPreviewGear", "Rebuild the preview's show-only list so equipment changes appear", C_LOGIC, ALL),
        ("EventGraph", "Each frame while shown: mouse drag turns the character, a quick second press resets the angle, right stick turns it", C_LOGIC, ["Tick", "Destruct"]),
    ],
    INV + "WBP_Menu_Inventory": [
        ("SetPreviewOpen", "Show or hide the character preview", C_LOGIC, ALL),
        ("ShutdownPreview", "Menu closing: destroy the character preview", C_LOGIC, ALL),
        ("TrackPreviewVisibility", "Every frame: when this page becomes visible or hidden (tab change), start or stop the preview", C_LOGIC, ALL),
        ("HandleActionResult", "Successful equip / unequip / drop: refresh the lists and the preview's equipment", C_LOGIC, ALL),
        ("RefreshDetailIfChanged", "Refresh or clear the detail panel only when the shown item's equipped state changed or it left the bag", C_LOGIC, ALL),
    ],
    MENU + "WBP_PlayerMenu": [
        ("CloseInventoryPreview", "Destroy the inventory page's character preview", C_LOGIC, ALL),
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
    unreal.log("%s 13G-5 DONE: %d comment boxes added" % (TAG, total))


main()
