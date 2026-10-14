"""
bh_comment_13bplus.py  (Phase 13B+)
Comment boxes for the compass copy: WBP_BH_Compass (a copy of Narrative Navigator's compass, kept out of the plugin).
Our one change to its logic is in UpdateMarkers: the marker's compass position now uses MapRangeClamped, so a marker outside the +-45 degree
window pins to the edge of the strip instead of leaving it.
Idempotent: a box is skipped when a comment node with the same text already exists in the graph.
Run: Tools > Execute Python Script > Content/Python/bh_comment_13bplus.py
"""
import unreal

TAG = "[BH_Comment]"
LIB = unreal.BH_BlueprintCommentLibrary
C_SETUP = unreal.LinearColor(0.10, 0.25, 0.45, 0.45)
C_LOGIC = unreal.LinearColor(0.12, 0.35, 0.18, 0.45)
C_VIEW = unreal.LinearColor(0.35, 0.12, 0.40, 0.45)

COMPASS = "/Game/BlackwoodHollow/UI/Compass/WBP_BH_Compass"

# (graph, box text, colour, [patterns]) - a node joins the box when any pattern is in its title or member name.
# Patterns are matched with spaces removed, case-insensitive.
BOXES = {
    COMPASS: [
        ("UpdateMarkers", "Compass position from the bearing to the marker, clamped to the strip edge (13B+ change)", C_VIEW,
         ["MapRangeClamped", "FindLookAtRotation", "NormalizeAxis", "SetPosition"]),
        ("UpdateMarkers", "Fade by distance; our markers push the fade distances out to the whole island", C_LOGIC,
         ["SetRenderOpacity", "MapRangeUnclamped", "GetMarkerStartFadeOutDistance", "GetMarkerStartFadeinDistance"]),
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
    unreal.log("%s FOLLOWUP DONE: %d comment boxes added" % (TAG, total))


main()