"""
bh_comment_13g.py  (Phase 13G)
Comment boxes for the Player Menu Blueprint work:
  WBP_PlayerMenu        13G-1 functions and the OnActivated / OnDeactivated chains
  PC_BlackwoodHollow    OpenPlayerMenu (the Door parameter and the System door tag) and the two menu input events
The 13G-2 pages (World Map, Quest Journal, System) are separate widgets; when they are built, add their graphs to BOXES.
Defensive by design: every asset, graph and node match is optional. A missing asset or graph, or a pattern that matches
no node, is logged as a warning and skipped; one failure never stops the rest. Every box added is logged.
Idempotent: a box is skipped when a comment node with the same text already exists in the graph.
A pattern list of ["*"] means "every node in that graph" (used for small function graphs, so no node-title guessing).
Run: Tools > Execute Python Script > Content/Python/bh_comment_13g.py, then read the Output Log for lines tagged [BH_Comment].
"""
import unreal

TAG = "[BH_Comment]"
LIB = unreal.BH_BlueprintCommentLibrary
C_LOGIC = unreal.LinearColor(0.12, 0.35, 0.18, 0.45)
C_VIEW = unreal.LinearColor(0.35, 0.12, 0.40, 0.45)
ALL = ["*"]

MENU = "/Game/BlackwoodHollow/UI/Menu/"

BOXES = {
    MENU + "WBP_PlayerMenu": [
        ("SetupTabs", "Register one tab per entry of TabOrder with its page from the page switcher, then label each tab", C_VIEW, ALL),
        ("LabelTab", "Set the tab's button text from TabLabels", C_VIEW, ALL),
        ("StepTab", "Q / E (LB / RB): move to the next or previous tab and wrap at both ends", C_LOGIC, ALL),
        ("StoreLastTab", "Remember the active tab for the next open (as an index), but never the System tab", C_LOGIC, ALL),
        ("SelectLastTab", "Open on the last-used tab (clamped to the tab count), then check whether a door asked for System", C_LOGIC, ALL),
        ("ApplyRequestedTab", "If RequestedTabId is set, jump to that tab and clear the request", C_LOGIC, ALL),
        ("OpenAtTab", "Open the menu on a named tab (World Map, Quest Journal, Inventory, Heart, System)", C_LOGIC, ALL),
        ("ApplyDoorRequest", "Workaround: Esc / Start sets the System tag on the player controller; if it is there, open on System", C_LOGIC, ALL),
        ("RefreshProgress", "Update Level, the XP bar and the XP text from the progression component", C_VIEW, ALL),
        ("ShowNotPausedCue", "Show the 'Game not paused' caption and hide it after NotPausedCueSeconds", C_VIEW, ALL),
        ("HideNotPausedCue", "Hide the 'Game not paused' caption", C_VIEW, ALL),
        ("EventGraph", "Menu opened: set up tabs, select the tab, refresh Level/XP, poll XP every 0.5 s, and show the first-open cue once", C_LOGIC,
         ["OnActivated", "SetupTabs", "SelectLastTab", "ApplyRequestedTab", "RefreshProgress", "SetTimer", "ConsumeNotPausedCue", "ShowNotPausedCue"]),
        ("EventGraph", "Menu closed: remember the tab and stop the XP and cue timers", C_LOGIC,
         ["OnDeactivated", "StoreLastTab", "ClearandInvalidate", "ClearTimer", "HideNotPausedCue"]),
    ],
    "/Game/Game/PC_BlackwoodHollow": [
        ("OpenPlayerMenu", "Open the Player Menu on the Menu layer: write the Door into the controller's tags (System = Esc / Start), then push the widget", C_LOGIC, ALL),
        ("EventGraph", "Tab / View: open the Player Menu on the last-used tab (Door = None)", C_LOGIC, ["IA_UI_OpenMenu"]),
        ("EventGraph", "Esc / Start: open the Player Menu on the System tab (Door = System)", C_LOGIC, ["IA_UI_OpenSystem"]),
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
        except Exception as e:  # graph name not found, or the library call failed
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
    unreal.log("%s 13G DONE: %d comment boxes added" % (TAG, total))


main()
