"""
bh_comment_13d.py  (Phase 13D)
Comment boxes for the graphs added or changed in the 13D enemy readability pass:
  WBP_OverheadVitals  OnEnemyPresented -> rank switch -> rank frame material
  WBP_TargetVitals    OnTargetPresented -> rank switch -> rank frame material
  WBP_BossHealthBar   OnBossPresented -> title card (ShowCard, timed HideCard) and rank frame
Idempotent: a box is skipped when a comment node with the same text already exists in the graph.
Run: Tools > Execute Python Script > Content/Python/bh_comment_13d.py
Each box wraps the nodes found by walking from its named seed nodes (title / member match).
"""
import unreal

TAG = "[BH_Comment]"
LIB = unreal.BH_BlueprintCommentLibrary
C_LOGIC = unreal.LinearColor(0.12, 0.35, 0.18, 0.45)
C_VIEW = unreal.LinearColor(0.35, 0.12, 0.40, 0.45)

UI = "/Game/BlackwoodHollow/UI/"

BOXES = {
    UI + "WBP_OverheadVitals": [
        ("EventGraph", "Enemy presented (from C++): frame the health bar with the material for its rank", C_LOGIC,
         ["OnEnemyPresented", "SwitchonEBH_EnemyRank", "SetBrushResourceObject", "Img_RankFrame"]),
    ],
    UI + "WBP_TargetVitals": [
        ("EventGraph", "Locked target presented (from C++): frame the health bar with the material for its rank", C_LOGIC,
         ["OnTargetPresented", "SwitchonEBH_EnemyRank", "SetBrushResourceObject", "Img_RankFrame"]),
    ],
    UI + "WBP_BossHealthBar": [
        ("EventGraph", "Boss presented (from C++): set the title card text, show it on first engage, hide it after 2.5 s, frame the bar by rank", C_LOGIC,
         ["OnBossPresented", "SetText", "SetTimerbyFunctionName", "ShowCard", "SwitchonEBH_EnemyRank", "SetBrushResourceObject", "Img_RankFrame"]),
        ("ShowCard", "Reveal the title card only when this is the first engage", C_VIEW,
         ["ShowCard", "Branch", "SetVisibility", "Txt_Card"]),
        ("HideCard", "Collapse the title card (called by the 2.5 s timer)", C_VIEW,
         ["HideCard", "SetVisibility", "Txt_Card"]),
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
    unreal.log("%s 13D DONE: %d comment boxes added" % (TAG, total))


main()
