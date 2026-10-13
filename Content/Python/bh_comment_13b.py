"""
bh_comment_13b.py  (Phase 13B follow-up)
Adds comment boxes and node comments to the 13B Blueprints. Idempotent: skips anything already commented.

Requires the BlackwoodHollowBetaEditor module (UBH_BlueprintCommentLibrary), built via BH_BuildAndLaunch.bat.
Run in the editor: Tools > Execute Python Script > Content/Python/bh_comment_13b.py
(or Output Log, Python mode:  py "Content/Python/bh_comment_13b.py")
Logs are prefixed [BH_Comment]. See README_comments.md for the reusable pattern.

How it finds nodes: it calls ListGraphNodes() and matches on the node TITLE (case-insensitive substring),
so it does not depend on K2Node_* names. Reorder/rename-safe; unmatched patterns are reported, not fatal.
"""
import unreal

TAG = "[BH_Comment]"
LIB = unreal.BH_BlueprintCommentLibrary

# Colours (match the 13A palette feel: muted, readable on the dark graph background)
C_SETUP = unreal.LinearColor(0.10, 0.25, 0.45, 0.45)   # blue   - setup / init
C_INPUT = unreal.LinearColor(0.45, 0.25, 0.08, 0.45)   # amber  - input handling
C_LOGIC = unreal.LinearColor(0.12, 0.35, 0.18, 0.45)   # green  - logic / helpers
C_VIEW  = unreal.LinearColor(0.35, 0.12, 0.40, 0.45)   # purple - visuals / glyphs

BP_DIR = "/Game/BlackwoodHollow/UI"
PC = "/Game/Game/PC_BlackwoodHollow"

# Each blueprint: {graph: {"box": (text, colour) or None, "notes": [(title_pattern, comment), ...]}}
# "box" wraps every node in the graph; "notes" attach a pinned bubble to the first node whose title contains the pattern.
PLAN = {
    BP_DIR + "/Menu/WBP_PlayerMenu": {
        "EventGraph": {
            "box": ("Activation: build tabs when shown, remember the tab when hidden", C_SETUP),
            "notes": [
                ("On Activated", "Runs every time the menu is pushed on the layer. Tabs are rebuilt here so a fresh widget and a re-shown one behave the same."),
                ("On Deactivated", "Remember the tab so the menu reopens where the player left it (stored locally, never replicated)."),
            ],
        },
        "SetupTabs": {
            "box": ("Register INVENTORY / MAP / QUESTS and link the page switcher", C_SETUP),
            "notes": [
                ("Set Linked Switcher", "Tab selection drives the page switcher, so pages never need manual show/hide."),
                ("Register Tab", "Page index matches switcher order: 0 Inventory, 1 Map, 2 Quests. Keep them in sync when adding tabs."),
            ],
        },
        "LabelTab": {"box": ("Set the tab button's text via its own style", C_VIEW), "notes": []},
        "SelectLastTab": {"box": ("Restore last-used tab from the layout subsystem", C_LOGIC), "notes": []},
        "StoreLastTab": {"box": ("Save the selected tab index for next open", C_LOGIC), "notes": []},
        "StepTab": {
            "box": ("Q/E: move selection by one with wrap-around", C_INPUT),
            "notes": [
                ("%", "Modulo wraps 2 -> 0 and 0 -> 2 without special cases."),
            ],
        },
        "IsKeyForAction": {
            "box": ("Does this key belong to that input action (keyboard or gamepad)?", C_INPUT),
            "notes": [
                ("Culture Invariant", "Key structs have no equality node; comparing invariant names also respects player rebinding."),
            ],
        },
        "OnPreviewKeyDown": {
            "box": ("Close / tab-step keys, independent of CommonUI input actions", C_INPUT),
            "notes": [
                ("Deactivate", "Own handling because the CommonUI enhanced back action is not reliable here and Esc is stolen by PIE."),
                ("Handled", "Handled stops the key reaching gameplay while the menu is open."),
            ],
        },
        "GetDesiredFocusTarget": {"box": ("Give focus to the selected tab so keys reach this widget", C_LOGIC), "notes": []},
    },
    BP_DIR + "/Menu/WBP_UI_TabList": {
        "EventGraph": {
            "box": ("CommonUI does not place tab buttons - we add/remove them in the HorizontalBox", C_SETUP),
            "notes": [
                ("Handle Tab Creation", "Required override: without it the list creates buttons but never parents them, so no tabs show."),
                ("Handle Tab Removal", "Mirror of creation so re-registering tabs does not leave stale buttons."),
            ],
        },
    },
    BP_DIR + "/Menu/WBP_UI_TabButton": {
        "EventGraph": {"box": ("Tab button visuals (BS_Tab style)", C_VIEW), "notes": []},
    },
    BP_DIR + "/Style/Glyphs/WBP_UI_InputGlyph": {
        "ResolveKeyNamesFromAction": {
            "box": ("Action -> current key names for keyboard and gamepad", C_LOGIC),
            "notes": [
                ("Culture Invariant", "Key names are the DataTable row names, so rebinding updates the glyph automatically."),
            ],
        },
        "ApplyGlyphFallbacks": {
            "box": ("If a glyph row/texture is missing, show the key name as text", C_VIEW),
            "notes": [
                ("Does Data Table Row Exist", "Avoids magenta missing-image boxes for keys without art (e.g. Tilde, F10)."),
            ],
        },
        "RefreshGlyph": {
            "box": ("Rebuild the glyph for the active input type", C_VIEW),
            "notes": [],
        },
    },
    PC: {
        "OpenPlayerMenu": {
            "box": ("Local controller only: open the player menu once", C_INPUT),
            "notes": [
                ("Is Any Menu Open", "Guard so the open key (also the close key) cannot stack two menus."),
                ("Is Local", "UI is local-only per player; the server never creates it for remote clients."),
            ],
        },
        "EventGraph": {
            "box": None,
            "notes": [
                ("Ensure", "Layout is created at BeginPlay so any system can push a screen later."),
                ("IA_UI_OpenMenu", "Started -> OpenPlayerMenu. Tab; mapped in IMC_Menu/IMC_UI, rebindable."),
                ("IA_UI_OpenSystem", "Started -> OpenPlayerMenu for now; the system menu will get its own page in a later phase."),
            ],
        },
    },
}


def parse(lines):
    """'Name | Title | X,Y' -> [(name, title)] ; comment nodes are skipped."""
    out = []
    for line in lines:
        parts = [p.strip() for p in str(line).split("|")]
        if len(parts) >= 3 and not parts[0].startswith("EdGraphNode_Comment"):
            out.append((parts[0], parts[1]))
    return out


def process(bp_path, plan):
    bp = unreal.load_asset(bp_path)
    if not bp:
        unreal.log_warning("%s missing blueprint %s" % (TAG, bp_path))
        return 0, 0, False
    boxes = notes = 0
    for graph, spec in plan.items():
        nodes = parse(LIB.list_graph_nodes(bp, graph))
        if not nodes:
            unreal.log_warning("%s %s: graph '%s' not found or empty" % (TAG, bp_path.split("/")[-1], graph))
            continue
        box = spec.get("box")
        existing = [str(l) for l in LIB.list_graph_nodes(bp, graph) if "EdGraphNode_Comment" in str(l) or "Comment" in str(l).split("|")[0]]
        if box and not existing:
            name = LIB.add_comment_box(bp, graph, [n for n, _ in nodes], box[0], box[1], 40)
            if name:
                boxes += 1
        for pattern, text in spec.get("notes", []):
            hit = next((n for n, t in nodes if pattern.lower() in t.lower()), None)
            if hit is None:
                unreal.log_warning("%s %s/%s: no node titled like '%s'" % (TAG, bp_path.split("/")[-1], graph, pattern))
                continue
            if LIB.set_node_comment(bp, graph, hit, text, True):
                notes += 1
    ok = LIB.compile_blueprint(bp)
    saved = LIB.save_blueprint(bp)
    unreal.log("%s %s: %d boxes, %d notes, compiled=%s saved=%s" % (TAG, bp_path.split("/")[-1], boxes, notes, ok, saved))
    return boxes, notes, ok and saved


def main():
    tb = tn = 0
    failed = []
    for path, plan in PLAN.items():
        b, n, ok = process(path, plan)
        tb += b
        tn += n
        if not ok:
            failed.append(path)
    unreal.log("%s DONE: %d comment boxes, %d node comments. Problems: %s" % (TAG, tb, tn, failed or "none"))


main()