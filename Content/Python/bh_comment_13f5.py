"""
bh_comment_13f5.py  (Phase 13F-5)
Comment boxes for the screen-state feedback graphs:
  WBP_LowHealthFeedback  functions + Construct / Tick
  WBP_BlightCreep        SetStacks + Tick
  WBP_BlightStatus       OnBlightStacksChanged, EnsureCreepWidget, Destruct
  WBP_HUD_Main           Construct (binds), the three bound events, SyncLowHealth, Tick, UpdateHintBannerOffset
  WBP_VitalsCluster      FlashStaminaBar, ClearStaminaFlash
Defensive by design: every asset, graph and node match is optional. A missing asset or graph, or a pattern that matches
no node, is logged as a warning and skipped; one failure never stops the rest. Every box added is logged.
Idempotent: a box is skipped when a comment node with the same text already exists in the graph.
A pattern list of ["*"] means "every node in that graph" (used for small function graphs, so no node-title guessing).
Run: Tools > Execute Python Script > Content/Python/bh_comment_13f5.py, then read the Output Log for lines tagged [BH_Comment].
"""
import unreal

TAG = "[BH_Comment]"
LIB = unreal.BH_BlueprintCommentLibrary
C_LOGIC = unreal.LinearColor(0.12, 0.35, 0.18, 0.45)
C_VIEW = unreal.LinearColor(0.35, 0.12, 0.40, 0.45)
ALL = ["*"]

UI = "/Game/BlackwoodHollow/UI/"
SS = UI + "ScreenState/"

BOXES = {
    SS + "WBP_LowHealthFeedback": [
        ("SetLowHealth", "Low health on or off (only acts when the state changes): start or stop the heartbeat and fade the vignette", C_LOGIC, ALL),
        ("SetHealthPercent", "Turn the health fraction into a 0 to 1 severity (0 at the low-health threshold, 1 at zero health), then refresh the targets", C_LOGIC, ALL),
        ("RefreshTarget", "Work out the vignette target intensity and heartbeat volume and rate from the current severity", C_LOGIC, ALL),
        ("StartHeartbeat", "Spawn the 2D heartbeat sound, fade it in and set its beats per minute", C_VIEW, ALL),
        ("StopHeartbeat", "Fade the heartbeat out and stop it", C_VIEW, ALL),
        ("ApplyHeartbeatRate", "Push the current beats per minute and volume to the playing heartbeat", C_VIEW, ALL),
        ("UpdateVignette", "Each frame: ease the vignette intensity toward its target at a constant speed and add the heartbeat throb", C_VIEW, ALL),
        ("FadeComponentIn", "Helper: fade an audio component in", C_LOGIC, ALL),
        ("FadeComponentOut", "Helper: fade an audio component out", C_LOGIC, ALL),
        ("SetMaterialScalar", "Helper: set one scalar parameter on the vignette material instance", C_VIEW, ALL),
        ("CreateImageMID", "Helper: make the vignette's dynamic material instance from the image brush", C_VIEW, ALL),
        ("EventGraph", "Construct: create the vignette material instance and start with everything off", C_VIEW, ["Construct"]),
        ("EventGraph", "Tick: drive the vignette fade and throb (this widget is never collapsed, so it keeps ticking)", C_VIEW, ["Tick", "UpdateVignette"]),
    ],
    SS + "WBP_BlightCreep": [
        ("SetStacks", "Blight stacks changed: set the target creep as stacks over StacksForFull, capped at MaxCreep", C_LOGIC, ALL),
        ("EventGraph", "Construct: create the creep material instance", C_VIEW, ["Construct"]),
        ("EventGraph", "Tick: ease the displayed creep toward the target (faster rise than fall) and write it to the material", C_VIEW, ["Tick"]),
    ],
    UI + "WBP_BlightStatus": [
        ("EventGraph", "Blight stacks changed (also fires once on bind): make sure the edge-creep widget exists, then give it the new stack count", C_LOGIC,
         ["OnBlightStacksChanged", "EnsureCreepWidget", "SetStacks"]),
        ("EventGraph", "Destruct: remove the edge-creep widget from the screen", C_LOGIC, ["Destruct", "RemoveFromParent"]),
        ("EnsureCreepWidget", "Create the Blight edge-creep widget once, add it to the player screen behind the Rot vignette and cache it", C_VIEW, ALL),
    ],
    UI + "WBP_HUD_Main": [
        ("EventGraph", "Construct: bind the low-health and stamina-empty events and sync the low-health state so respawn and first value are correct", C_LOGIC,
         ["Construct", "OnLowHealthChanged", "OnStaminaEmpty", "SyncLowHealth", "Assign", "Bind"]),
        ("EventGraph", "Low health changed (bound to the HUD C++ event): start or stop the low-health feedback", C_LOGIC,
         ["OnLowHealthChanged_Event", "SetLowHealth", "LowHealthFeedback"]),
        ("EventGraph", "Stamina ran out (bound to the HUD C++ event): play the exhale sound and flash the stamina bar", C_LOGIC,
         ["OnStaminaEmpty_Event", "OnStaminaEmptyFeedback", "ExhaleSound", "FlashStaminaBar", "PlaySound2D"]),
        ("EventGraph", "Health updated: feed the health fraction to the low-health feedback and re-sync its state", C_LOGIC,
         ["On Health Updated", "OnHealthUpdated", "SetHealthPercent", "SyncLowHealth"]),
        ("EventGraph", "Tick: slide the hint banner below the target panel (or boss bar) when one is showing", C_VIEW,
         ["Event Tick", "UpdateHintBannerOffset"]),
        ("SyncLowHealth", "Read the HUD's current low-health state and pass it to the feedback widget", C_LOGIC, ALL),
        ("UpdateHintBannerOffset", "Measure the top-centre stack (boss bar plus target panel); move the hint banner under it, or back to 76 when it is empty, gliding with FInterpTo", C_VIEW, ALL),
    ],
    UI + "WBP_VitalsCluster": [
        ("FlashStaminaBar", "Stamina ran out: tint the bar, pop the row's scale, then clear both after StaminaFlashSeconds", C_VIEW, ALL),
        ("ClearStaminaFlash", "End of the stamina flash: restore the bar tint and the row scale", C_VIEW, ALL),
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


def process(path, boxes):
    added = 0
    bp = unreal.load_asset(path)
    if not bp:
        unreal.log_warning("%s MISSING asset %s (skipped %d boxes)" % (TAG, path, len(boxes)))
        return 0
    short = path.split("/")[-1]
    for graph, text, colour, patterns in boxes:
        try:
            nodes = parse(LIB.list_graph_nodes_ex(bp, graph))
        except Exception as e:  # graph name not found, or the library call failed
            unreal.log_warning("%s %s/%s: cannot list nodes (%s)" % (TAG, short, graph, e))
            continue
        if not nodes:
            unreal.log_warning("%s %s/%s: graph missing or empty, skipped '%s'" % (TAG, short, graph, text[:40]))
            continue
        if any(n.startswith("EdGraphNode_Comment") and norm(text) in norm(t) for n, t, _ in nodes):
            unreal.log("%s %s/%s: already exists '%s'" % (TAG, short, graph, text[:40]))
            continue
        real = [(n, t, m) for n, t, m in nodes if not n.startswith("EdGraphNode_Comment")]
        if patterns == ALL:
            hits = [n for n, t, m in real]
        else:
            pats = [norm(p) for p in patterns]
            hits = [n for n, t, m in real if any(p in norm(t) or (m and p in norm(m)) for p in pats)]
        if not hits:
            unreal.log_warning("%s %s/%s: no nodes matched for '%s'" % (TAG, short, graph, text[:40]))
            continue
        try:
            ok = LIB.add_comment_box(bp, graph, hits, text, colour, 40)
        except Exception as e:
            unreal.log_warning("%s %s/%s: add_comment_box failed (%s)" % (TAG, short, graph, e))
            continue
        if ok:
            added += 1
            unreal.log("%s %s/%s: ADDED box over %d nodes: %s" % (TAG, short, graph, len(hits), text[:60]))
        else:
            unreal.log_warning("%s %s/%s: add_comment_box returned false for '%s'" % (TAG, short, graph, text[:40]))
    try:
        LIB.compile_blueprint(bp)
        LIB.save_blueprint(bp)
    except Exception as e:
        unreal.log_warning("%s %s: compile or save failed (%s)" % (TAG, short, e))
    return added


def main():
    total = 0
    for path, boxes in BOXES.items():
        try:
            total += process(path, boxes)
        except Exception as e:
            unreal.log_warning("%s %s: unexpected error (%s)" % (TAG, path, e))
    unreal.log("%s 13F-5 DONE: %d comment boxes added" % (TAG, total))


main()
