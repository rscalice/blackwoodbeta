"""13B+ comment boxes. Run via Tools > Execute Python Script after the post-build Blueprints exist.
Uses BH_BlueprintCommentLibrary (see bh_comment_13b.py for the call pattern).
Planned boxes:
  WBP_Compass: "Bind to beat tracker", "Heading to strip position", "Objective marker", "Partner markers"
  BP_BH_BeatTarget/Trigger instances: none (C++ only)
"""
import unreal

BOXES = {
    "/Game/BlackwoodHollow/UI/Game/WBP_Compass": [
        ("Bind to beat tracker", "Construct"),
        ("Heading to strip position", "UpdateHeading"),
        ("Objective marker", "UpdateObjective"),
        ("Partner markers", "UpdatePartners"),
    ],
}

for path, boxes in BOXES.items():
    bp = unreal.load_asset(path)
    if not bp:
        unreal.log_warning("13B+ comments: missing " + path)
        continue
    for text, graph in boxes:
        unreal.BH_BlueprintCommentLibrary.add_comment_around_graph(bp, graph, text)
