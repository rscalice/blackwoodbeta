"""
bh_comment_13c.py  (Phase 13C)
Comment boxes for the graphs added or changed in the 13C HUD pass:
  WBP_FragmentBar        OnSlotReady -> FlashSlot (timer) -> ClearFlashes
  WBP_NotificationQueue  Shown / Updated -> TintTitle (Crimson warning, Gold level-up, else Bone)
  WBP_DeathPrompt, WBP_InteractPrompt  OnInitialized -> ApplyPalette (Crimson token for the cause / denied line)
Idempotent: a box is skipped when a comment node with the same text already exists in the graph.
Run: Tools > Execute Python Script > Content/Python/bh_comment_13c.py
Each box wraps the nodes found by walking from its named seed nodes (title / member match).
"""
import unreal

TAG = "[BH_Comment]"
LIB = unreal.BH_BlueprintCommentLibrary
C_SETUP = unreal.LinearColor(0.10, 0.25, 0.45, 0.45)
C_LOGIC = unreal.LinearColor(0.12, 0.35, 0.18, 0.45)
C_VIEW = unreal.LinearColor(0.35, 0.12, 0.40, 0.45)

UI = "/Game/BlackwoodHollow/UI/"

# (graph, box text, colour, [patterns]) - a node joins the box when any pattern is in its title or member name.
BOXES = {
    UI + "WBP_FragmentBar": [
        ("EventGraph", "Slot became ready (from C++): flash that slot's frame", C_LOGIC,
         ["OnSlotReady", "FlashSlot"]),
        ("FlashSlot", "Show the Flash image for the slot, then clear every flash after 0.2 s", C_VIEW,
         ["FlashSlot", "SetTimerbyFunctionName", "Switch", "SetRenderOpacity", "Flash_Slot"]),
        ("ClearFlashes", "Hide all three ready flashes", C_VIEW,
         ["ClearFlashes", "SetRenderOpacity", "Flash_Slot"]),
    ],
    UI + "WBP_NotificationQueue": [
        ("EventGraph", "Toast shown or updated: tint the title (Crimson for harm or death, Gold for level-up)", C_LOGIC,
         ["OnNotificationShown", "OnNotificationUpdated", "BreakBHNotification", "SwitchonEBH_NotificationKind", "TintTitle"]),
        ("TintTitle", "Pick the token from DA_UI_Palette: warning = Crimson, reward = Gold, else Bone", C_VIEW,
         ["TintTitle", "IsValid", "Select", "GetCrimson", "GetGold", "GetBone", "MakeSlateColor", "SetColorandOpacity", "GetPalette", "TxtTitle"]),
    ],
    UI + "WBP_DeathPrompt": [
        ("EventGraph", "Once on creation: apply the palette colours", C_SETUP,
         ["OnInitialized", "ApplyPalette"]),
        ("ApplyPalette", "Cause line uses the Crimson token from DA_UI_Palette", C_VIEW,
         ["ApplyPalette", "IsValid", "GetCrimson", "MakeSlateColor", "SetColorandOpacity", "GetPalette", "TxtCause"]),
    ],
    UI + "WBP_InteractPrompt": [
        ("EventGraph", "Once on creation: apply the palette colours", C_SETUP,
         ["OnInitialized", "ApplyPalette"]),
        ("ApplyPalette", "Denied line uses the Crimson token from DA_UI_Palette", C_VIEW,
         ["ApplyPalette", "IsValid", "GetCrimson", "MakeSlateColor", "SetColorandOpacity", "GetPalette", "TxtDenied"]),
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


def main():
    total = 0
    for path, boxes in BOXES.items():
        bp = unreal.load_asset(path)
        if not bp:
            unreal.log_warning("%s missing %s" % (TAG, path))
            continue
        for graph, text, colour, patterns in boxes:
            nodes = parse(LIB.list_graph_nodes_ex(bp, graph))
            if any(n.startswith("EdGraphNode_Comment") and norm(text) in norm(t) for n, t, _ in nodes):
                unreal.log("%s %s/%s: '%s' already exists" % (TAG, path.split("/")[-1], graph, text))
                continue
            pats = [norm(p) for p in patterns]
            hits = [n for n, t, m in nodes if not n.startswith("EdGraphNode_Comment")
                    and any(p in norm(t) or (m and p in norm(m)) for p in pats)]
            if not hits:
                unreal.log_warning("%s %s/%s: no nodes matched for '%s'" % (TAG, path.split("/")[-1], graph, text))
                continue
            if LIB.add_comment_box(bp, graph, hits, text, colour, 40):
                total += 1
        LIB.compile_blueprint(bp)
        LIB.save_blueprint(bp)
    unreal.log("%s 13C DONE: %d comment boxes added" % (TAG, total))


main()
