"""
bh_comment_13h.py  (Phase 13H-3 / 13H-4)
Comment boxes for the settings page, its row and modal widgets, the System tab and the heartbeat-duck hookup.
Same defensive, idempotent pattern as bh_comment_13f5.py (a missing asset, graph or node is skipped with a warning).
Run: Tools > Execute Python Script > Content/Python/bh_comment_13h.py, then read the Output Log for lines tagged [BH_Comment].
"""
import unreal

TAG = "[BH_Comment]"
LIB = unreal.BH_BlueprintCommentLibrary
C_LOGIC = unreal.LinearColor(0.12, 0.35, 0.18, 0.45)
C_VIEW = unreal.LinearColor(0.35, 0.12, 0.40, 0.45)
ALL = ["*"]

UI = "/Game/BlackwoodHollow/UI/"
ST = UI + "Menu/Settings/"

BOXES = {
    ST + "WBP_Menu_Settings": [
        ("BuildAudioRows", "Set up the four volume sliders (0 to 100 percent)", C_VIEW, ALL),
        ("BuildControlRows", "Set up mouse X and Y sensitivity, invert Y, gamepad look and rumble rows", C_VIEW, ALL),
        ("BuildCameraRows", "Set up the camera shake strength slider", C_VIEW, ALL),
        ("BuildVideoRows", "Set up quality, resolution, window mode, VSync and frame cap rows", C_VIEW, ALL),
        ("BuildCycleOptions", "Give each cycle row its list of choices", C_VIEW, ALL),
        ("BuildResolutionList", "Read the supported resolutions (adding the current one if missing) and build the row labels", C_LOGIC, ALL),
        ("BuildRows", "Build every row once when the page is created", C_LOGIC, ALL),
        ("RefreshNonVideoRows", "Show the saved audio, control and camera values on their rows without firing changes", C_LOGIC, ALL),
        ("RefreshVideoRows", "Show the current quality, resolution, window mode, VSync and frame cap on their rows", C_LOGIC, ALL),
        ("ApplyRowToSettings", "A row changed: audio, control and camera values apply live; video rows only mark the page as changed", C_LOGIC, ALL),
        ("ApplyVideoRowsToSettings", "Read the video rows and hand them to the engine display settings", C_LOGIC, ALL),
        ("ApplyChanges", "Apply: save non-video settings; if video changed, preview it with a keep-or-revert prompt", C_LOGIC, ALL),
        ("ShowVideoConfirm", "Remember the old display settings, apply the new ones and ask to keep them (10 second countdown)", C_LOGIC, ALL),
        ("KeepVideoSettings", "Player kept the new display settings: confirm and save them", C_LOGIC, ALL),
        ("RevertVideoSettings", "Declined or timed out: restore the old display settings and refresh the rows", C_LOGIC, ALL),
        ("RevertChanges", "Throw away unsaved changes: reload from the saved file and refresh every row", C_LOGIC, ALL),
        ("ResetToDefaults", "Reset audio, controls and camera to defaults and queue default video choices (applied with Apply)", C_LOGIC, ALL),
        ("OpenPage", "Page opened: rebuild the resolution list, load the rows and focus the first one", C_LOGIC, ALL),
        ("DiscardAndClose", "Page closing: cancel any open prompt and discard unsaved changes", C_LOGIC, ALL),
        ("HandleBack", "Back pressed: close the keep-or-revert prompt if it is open, otherwise let the page close", C_LOGIC, ALL),
        ("FocusFirst", "Give gamepad focus to the first row", C_VIEW, ALL),
        ("EventGraph", "Construct: build the rows, label the footer buttons, listen to every row and to the keep-or-revert prompt", C_LOGIC, ["Construct", "GetAllChildren", "Assign", "SetLabels"]),
        ("EventGraph", "A row changed: apply it to the settings", C_LOGIC, ["OnRowChanged_Event", "ApplyRowToSettings"]),
        ("EventGraph", "Footer buttons and the keep-or-revert prompt", C_LOGIC, ["OnClicked", "ApplyChanges", "RevertChanges", "ResetToDefaults", "OnConfirmed_Event", "OnCancelled_Event", "KeepVideoSettings", "RevertVideoSettings"]),
    ],
    ST + "WBP_Settings_Row": [
        ("Configure", "Set the row's id, label and slider range (or switch it to a cycle button)", C_VIEW, ALL),
        ("SetCycleOptions", "Give a cycle row its choices and select the first", C_VIEW, ALL),
        ("SetValueSilent", "Show a value on the row without telling the page", C_LOGIC, ALL),
        ("CycleNext", "Cycle button clicked: step to the next choice (wrapping) and tell the page", C_LOGIC, ALL),
        ("HandleSliderChanged", "Slider moved: refresh the value text and tell the page", C_LOGIC, ALL),
        ("RefreshValueText", "Show the slider value as a percent or a number", C_VIEW, ALL),
        ("RefreshCycleText", "Show the row label and the current choice on the cycle button", C_VIEW, ALL),
        ("FocusRow", "Give gamepad focus to this row's control", C_VIEW, ALL),
        ("GetRowValue", "Read the row's current value", C_LOGIC, ALL),
    ],
    ST + "WBP_UI_ConfirmModal": [
        ("ShowModal", "Open the prompt: set its texts, start the countdown if timed, focus Cancel", C_VIEW, ALL),
        ("HideModal", "Close the prompt and stop its timer", C_VIEW, ALL),
        ("UpdateCountdownText", "Write the seconds remaining", C_VIEW, ALL),
        ("TickCountdown", "Each second: count down and cancel when it reaches zero", C_LOGIC, ALL),
        ("CancelModal", "Close the prompt and report Cancel", C_LOGIC, ALL),
        ("ConfirmModal", "Close the prompt and report Confirm", C_LOGIC, ALL),
    ],
    UI + "Menu/WBP_Menu_System": [
        ("OpenSettings", "Settings pressed: hide the buttons, show the settings page", C_LOGIC, ALL),
        ("CloseSettings", "Close the settings page (discarding unsaved changes) and bring the buttons back", C_LOGIC, ALL),
        ("ShowQuitModal", "Ask to quit; the host is warned that it ends the session for everyone", C_LOGIC, ALL),
        ("QuitConfirmed", "Quit confirmed: leave the game", C_LOGIC, ALL),
        ("TryGoBack", "Back pressed: close the quit prompt or the settings page first; only then let the menu close", C_LOGIC, ALL),
        ("CloseSubpanels", "Menu closing: cancel the quit prompt and leave the settings page", C_LOGIC, ALL),
        ("FocusFirst", "Focus the settings page when it is open, otherwise the Resume button", C_VIEW, ALL),
        ("EventGraph", "Construct: label the buttons and listen to Resume and the quit prompt", C_VIEW, ["Construct", "SetLabels", "Assign"]),
        ("EventGraph", "Buttons and prompt answers: Resume, Settings, Quit, confirm and cancel", C_LOGIC, ["OnButtonBaseClicked_Event", "OnClicked", "OnConfirmed_Event", "OnCancelled_Event", "OpenSettings", "ShowQuitModal", "QuitConfirmed", "CallOnResumeRequested"]),
    ],
    UI + "Menu/WBP_PlayerMenu": [
        ("OnPreviewKeyDown", "Back, Esc or the menu key: close the inventory context menu or the System sub-page first, then the whole menu; shoulders step tabs", C_LOGIC, ALL),
        ("EventGraph", "Menu closing: leave any System sub-page first (cancels the quit prompt, discards unsaved settings)", C_LOGIC, ["OnDeactivated", "CloseSubpanels"]),
    ],
    UI + "ScreenState/WBP_LowHealthFeedback": [
        ("EventGraph", "Heartbeat ducking: when the player controller's setting changes, update the heartbeat volume multiplier", C_LOGIC,
         ["OnHeartbeatDuckChanged_Event", "SetHeartbeatDuck"]),
        ("EventGraph", "Construct: bind the heartbeat duck setting so the volume follows the player's slider", C_LOGIC,
         ["AssignOnHeartbeatDuckChanged", "GetHeartbeatVolumeMultiplier", "CastToBH_PlayerController", "GetOwningPlayer"]),
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

