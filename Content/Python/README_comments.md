# Blueprint comments from scripts

MCP cannot set `NodeComment` or create comment boxes, so agents use `UBH_BlueprintCommentLibrary`
(editor module `BlackwoodHollowBetaEditor`) from a Python script that Robert runs once.

## Run
1. Build once with BH_BuildAndLaunch.bat (editor closed).
2. In the editor: Tools > Execute Python Script > `Content/Python/bh_comment_13b.py`.
3. Check the Output Log for `[BH_Comment]` lines. Re-running is safe: graphs that already have a comment box are not boxed again; node comments are simply overwritten.

## API (`unreal.BH_BlueprintCommentLibrary`, all static)
| Call | Purpose |
|---|---|
| `list_graph_nodes(bp, "Graph")` | `"Name \| Title \| X,Y"` per node. Pass `"None"`/empty name for all graphs (lines prefixed `[Graph]`). |
| `set_node_comment(bp, "Graph", "K2Node_CallFunction_3", "why", True)` | Pinned bubble on one node. |
| `add_comment_box(bp, "Graph", ["NodeA", "NodeB"], "Section title", unreal.LinearColor(r,g,b,a), 40)` | Box around the nodes; returns the comment node name. |
| `compile_blueprint(bp)` / `save_blueprint(bp)` | Compile (widget BPs too) and save without prompts. |

Graph search covers event graphs, functions, macros and delegate signatures, so widget Blueprints work the same way.

## Pattern for a new phase
1. Agent builds graphs with MCP as usual (logic in functions, left-to-right).
2. Copy `bh_comment_13b.py`, edit the `PLAN` dict: per Blueprint, per graph, a box title + colour and `(title pattern, comment)` notes. Patterns match node titles (case-insensitive substring), so no K2Node names are needed.
3. Comments say *why*, not what; keep them to a sentence.
4. Robert runs it; the agent commits the resulting `.uasset` changes.

Colours used in 13B: blue = setup, amber = input, green = logic, purple = visuals.