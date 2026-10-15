"""
bh_comment_13e.py  (Phase 13E)
Comment boxes for the graphs added or changed in the 13E hint pass:
  WBP_NotificationQueue  ApplyHintLayout     backing material, glyph row visibility, per-glyph input action
  WBP_NotificationQueue  OnNotificationShown layout reset per kind; Hint kind uses the Bone title
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
    UI + "WBP_NotificationQueue": [
        ("ApplyHintLayout", "Pick the toast backing for the kind (hint with glyphs, hint, or standard) and show the glyph row only when the hint has input actions", C_VIEW,
         ["ApplyHintLayout", "BreakBHNotification", "SetBrushResourceObject", "Img_Backing", "Row_Glyphs", "Length"]),
        ("ApplyHintLayout", "Fill up to three input glyphs from the hint's actions; collapse the unused ones", C_VIEW,
         ["SetInputAction", "RefreshGlyph", "Glyph_0", "Glyph_1", "Glyph_2", "Get"]),
        ("EventGraph", "Notification shown (from C++): reset the layout for every kind, then tint the title (hints use the Bone title)", C_LOGIC,
         ["OnNotificationShown", "SwitchonEBH_NotificationKind", "ApplyHintLayout", "TintTitle"]),
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
