"""
bh_comment_13e.py  (Phase 13E)
Comment boxes for the graphs added or changed in the 13E hint pass:
  WBP_HintBanner         OnHintPresented      title, body and the one primary-key glyph (hints show in the objective slot)
  WBP_UI_InputGlyph      ApplyGlyphFallbacks  key name as text when a glyph image row is missing
  WBP_UI_InputGlyph      ApplyDisplayNames    readable key names (Mouse 4, Space, Shift, Ctrl, ~) and WASD for IA_Move
  WBP_NotificationQueue  OnNotificationShown  tint the title per kind (ApplyHintLayout was removed; hints no longer use the toast queue)
Idempotent: a box is skipped when a comment node with the same text already exists in the graph.
Run: Tools > Execute Python Script > Content/Python/bh_comment_13e.py
"""
import unreal

TAG = "[BH_Comment]"
LIB = unreal.BH_BlueprintCommentLibrary
C_LOGIC = unreal.LinearColor(0.12, 0.35, 0.18, 0.45)
C_VIEW = unreal.LinearColor(0.35, 0.12, 0.40, 0.45)

UI = "/Game/BlackwoodHollow/UI/"

BOXES = {
    UI + "WBP_HintBanner": [
        ("EventGraph", "Hint presented (from C++): set the title and body, and show one glyph for the primary key on the active device", C_VIEW,
         ["OnHintPresented", "SetText", "SetInputAction", "RefreshGlyph", "SetVisibility", "Length", "Glyph_0", "Txt_Title", "Txt_Body"]),
    ],
    UI + "Style/Glyphs/WBP_UI_InputGlyph": [
        ("ApplyGlyphFallbacks", "When a key has no glyph image row, show its name as text instead of the missing-image box (keyboard, Xbox and PlayStation)", C_VIEW,
         ["DoesDataTableRowExist", "SetText", "ApplyLabelVisibility", "ApplyDisplayNames"]),
        ("ApplyDisplayNames", "Readable key names for keys without an image (Mouse 4, Space, Shift, Ctrl, ~), and WASD for the Move action", C_VIEW,
         ["EqualExactly", "SetText", "DoesDataTableRowExist", "GetObjectName"]),
    ],
    UI + "WBP_NotificationQueue": [
        ("EventGraph", "Notification shown (from C++): tint the title for the kind (hints use the objective-slot banner, not this queue)", C_LOGIC,
         ["OnNotificationShown", "SwitchonEBH_NotificationKind", "TintTitle"]),
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
    unreal.log("%s 13E DONE: %d comment boxes added" % (TAG, total))


main()
