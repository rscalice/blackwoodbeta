"""
bh_comment_13f2.py  (Phase 13F-2/3)
Comment boxes for the hit-feel listener:
  BP_BH_HitFeel  EventGraph / PlayHitFeedback / ApplyFeedback / PlaySurfaceLayer / PlayHeavyAndSparks / SpawnSparks / PlayCueLayer
  PC_BlackwoodHollow  SpawnHitFeel
Idempotent: a box is skipped when a comment node with the same text already exists in the graph.
Run: Tools > Execute Python Script > Content/Python/bh_comment_13f2.py
"""
import unreal

TAG = "[BH_Comment]"
LIB = unreal.BH_BlueprintCommentLibrary
C_LOGIC = unreal.LinearColor(0.12, 0.35, 0.18, 0.45)
C_VIEW = unreal.LinearColor(0.35, 0.12, 0.40, 0.45)

FEEL = "/Game/BlackwoodHollow/Combat/Feel/BP_BH_HitFeel"

BOXES = {
    FEEL: [
        ("EventGraph", "Local only: bind the hit-feedback subsystem when this actor is spawned (owner = local player controller)", C_LOGIC,
         ["BeginPlay", "Assign", "GetHitFeedbackSubsystem"]),
        ("EventGraph", "Hit feedback arrived (every machine that ran the combat cue): hand the payload to the sound and spark logic", C_LOGIC,
         ["OnHitFeedback_Event", "PlayHitFeedback"]),
        ("PlayHitFeedback", "Pick the tier volume scale (Light, Medium, Heavy, Massive) from TierVolume and flag heavy hits", C_LOGIC,
         ["Break", "Switch", "ApplyFeedback", "Get", "TierVolume"]),
        ("ApplyFeedback", "By outcome: Hit adds surface layer, heavy layer and sparks; Block adds sparks; Perfect block adds the clang and bigger sparks; Posture break adds the accent", C_LOGIC,
         ["Break", "Switch", "PlaySurfaceLayer", "PlayHeavyAndSparks", "SpawnSparks", "PlayCueLayer", "Get Layer"]),
        ("PlaySurfaceLayer", "Extra impact layer for the surface hit (Flesh, Armour, Shell)", C_LOGIC,
         ["Break", "Switch", "PlayCueLayer", "Get Layer"]),
        ("PlayHeavyAndSparks", "Sparks on every hit; the low heavy-impact layer only for Heavy and Massive tiers", C_LOGIC,
         ["Break", "SpawnSparks", "Branch", "PlayCueLayer", "Get Layer"]),
        ("SpawnSparks", "Spark burst at the impact point along the hit direction (purple ichor when the victim is Blighted), scaled by tier", C_VIEW,
         ["Select", "Get Spark", "Spawn", "Make", "Rot"]),
        ("PlayCueLayer", "Play one random sound from a layer at the impact point (random volume and pitch inside the layer's ranges, scaled by tier)", C_LOGIC,
         ["Break", "Length", "Random", "Play Sound", "Greater", "Multiply", "Get Sound"]),
    ],
    "/Game/Game/PC_BlackwoodHollow": [
        ("SpawnHitFeel", "Local player only: spawn the hit-feel listener (not replicated, never on a dedicated server or for a remote player's controller)", C_LOGIC,
         ["IsLocalController", "Branch", "Spawn", "Make Transform", "Self"]),
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
    unreal.log("%s 13F-2 DONE: %d comment boxes added" % (TAG, total))


main()
