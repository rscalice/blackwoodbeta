"""Adds comment boxes to the 13I-3 partner panel Blueprints.
Run in the editor: Tools > Execute Python Script. Wraps nodes by position lane.
Comment boxes cannot be created through MCP, so this is run by hand once.
"""
import unreal

ROW = "/Game/BlackwoodHollow/UI/WBP_HUD_PartnerRow"
PANEL = "/Game/BlackwoodHollow/UI/WBP_HUD_PartnerPanel"

# (asset, graph name, y_min, y_max, title)
LANES = [
    (ROW, "EventGraph", -400, -100, "Row: construct, mark tree ready, first refresh"),
    (ROW, "EventGraph", -100, 300, "Row: bind partner status delegate"),
    (ROW, "EventGraph", 300, 700, "Row: on status changed, refresh and notify panel"),
    (PANEL, "EventGraph", 100, 450, "Panel: init and poll timer"),
    (PANEL, "EventGraph", 500, 1100, "Panel: bind row events, rebuild on change"),
    (PANEL, "EventGraph", 1100, 1550, "Panel: bind GameState events"),
    (PANEL, "EventGraph", 1550, 2300, "Panel: roster change and disconnect display"),
]

def add_comment(bp, graph_name, y_min, y_max, title):
    graph = None
    for g in unreal.BlueprintEditorLibrary.get_graphs(bp) if hasattr(unreal.BlueprintEditorLibrary, "get_graphs") else []:
        if g.get_name() == graph_name:
            graph = g
    if graph is None:
        unreal.log_warning("Graph %s not found; add comment '%s' by hand" % (graph_name, title))
        return
    nodes = [n for n in graph.nodes if y_min <= n.node_pos_y < y_max]
    if not nodes:
        return
    x0 = min(n.node_pos_x for n in nodes) - 40
    y0 = min(n.node_pos_y for n in nodes) - 80
    x1 = max(n.node_pos_x for n in nodes) + 400
    y1 = max(n.node_pos_y for n in nodes) + 200
    c = unreal.EdGraphNode_Comment()
    c.node_comment = title
    c.node_pos_x, c.node_pos_y = x0, y0
    c.node_width, c.node_height = x1 - x0, y1 - y0
    graph.add_node(c)

for path, graph, lo, hi, title in LANES:
    bp = unreal.load_asset(path)
    add_comment(bp, graph, lo, hi, title)
    unreal.EditorAssetLibrary.save_loaded_asset(bp)

# Function graphs (ApplyStatus, RefreshPolled, RefreshStatus, SetPartner, ShowDisconnected,
# ConfigureRow, RebuildRows, PollRows, ClearDisconnected, TryBindGameState,
# StartDisconnectDisplay, InitPanel): select all nodes in each and press C to comment.
unreal.log("13I comment script done; check the logs for any graph not found.")
