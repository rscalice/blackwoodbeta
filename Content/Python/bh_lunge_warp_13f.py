"""
Blackwood Hollow 13F-1: add Motion Warping notify states (target BH_Lunge) to the
Sword combo montage, one per combo step, spanning only that step's root-travel window.

Run once from the editor: Tools > Execute Python Script > this file.
Re-runnable: it deletes and rebuilds the 'Lunge' notify track each time.
Log: Saved/bh_lunge_warp_13f.log  (also printed to the Output Log).

It also prints the measured forward travel per step (cm) -> use these for
LungeExpectedReach on the Sword combo ability.
"""
import os
import unreal

MONTAGE_PATH = "/Game/BlackwoodHollow/Animation/Stances/Sword/Combat/Montages/AM_Sword_MM_Combo_A"
CLIP_SEARCH_ROOT = "/Game/BlackwoodHollow/Animation/Stances/Sword"
CLIP_NAMES = ["AS_Sword_CB_Combo_1", "AS_Sword_CB_Combo_2", "AS_Sword_CB_Combo_3"]
# Fallback montage-time start of each clip's segment (seconds), used only if the
# montage's segment list cannot be read from Python.
FALLBACK_SEGMENT_STARTS = [0.0, 0.75, 1.40]
ROOT_BONE = "root"
TRACK_NAME = "Lunge"
WARP_TARGET = "BH_Lunge"
SPEED_FRACTION = 0.20      # window = samples moving faster than this fraction of peak speed
PAD_SECONDS = 0.02         # pad the window a little on both sides
SAMPLE_HZ = 30.0

LOG_LINES = []


def log(msg):
    LOG_LINES.append(msg)
    unreal.log("[BH Lunge 13F] " + msg)


def flush_log():
    try:
        path = os.path.join(unreal.Paths.project_saved_dir(), "bh_lunge_warp_13f.log")
        with open(path, "w", encoding="utf-8") as f:
            f.write("\n".join(LOG_LINES))
    except Exception as exc:  # noqa
        unreal.log_warning("[BH Lunge 13F] could not write log file: %s" % exc)


def find_clip(name):
    for asset_path in unreal.EditorAssetLibrary.list_assets(CLIP_SEARCH_ROOT, recursive=True, include_folder=False):
        base = asset_path.split(".")[0].split("/")[-1]
        if base == name:
            return unreal.EditorAssetLibrary.load_asset(asset_path)
    return None


def root_xy_samples(clip):
    """Return (times, xy_positions_cm) of the root bone's local translation."""
    length = clip.get_editor_property("sequence_length") if hasattr(clip, "get_editor_property") else clip.get_play_length()
    steps = max(2, int(round(length * SAMPLE_HZ)) + 1)
    times, pts = [], []
    for i in range(steps):
        t = min(length, i / SAMPLE_HZ)
        xf = unreal.AnimationLibrary.get_bone_pose_for_time(clip, ROOT_BONE, t, False)
        times.append(t)
        pts.append((xf.translation.x, xf.translation.y))
    return length, times, pts


def lunge_window(times, pts):
    """Return (start_t, end_t, travel_cm) of the main root-travel span, or None."""
    speeds = []
    for i in range(1, len(pts)):
        dt = times[i] - times[i - 1]
        dx = pts[i][0] - pts[i - 1][0]
        dy = pts[i][1] - pts[i - 1][1]
        speeds.append(((dx * dx + dy * dy) ** 0.5) / dt if dt > 0 else 0.0)
    if not speeds:
        return None
    peak = max(speeds)
    if peak < 1.0:
        return None
    thresh = peak * SPEED_FRACTION
    # largest contiguous run above threshold (by travelled distance)
    best, cur, best_dist, cur_dist = None, None, 0.0, 0.0
    for i, s in enumerate(speeds):
        step_dist = s * (times[i + 1] - times[i])
        if s >= thresh:
            if cur is None:
                cur, cur_dist = [i, i], 0.0
            cur[1] = i
            cur_dist += step_dist
            if cur_dist > best_dist:
                best, best_dist = list(cur), cur_dist
        else:
            cur = None
    if best is None:
        return None
    start_t = times[best[0]]
    end_t = times[best[1] + 1]
    return start_t, end_t, best_dist


def read_segments(montage):
    """Try to read (montage_start, clip_start, rate) per segment of slot track 0."""
    try:
        tracks = montage.get_editor_property("slot_anim_tracks")
        segs = tracks[0].get_editor_property("anim_track").get_editor_property("anim_segments")
        out = []
        for seg in segs:
            out.append((
                float(seg.get_editor_property("start_pos")),
                float(seg.get_editor_property("anim_start_time")),
                float(seg.get_editor_property("anim_play_rate")),
            ))
        return out
    except Exception as exc:  # noqa
        log("Could not read montage segments (%s); using fallback starts %s" % (exc, FALLBACK_SEGMENT_STARTS))
        return None


def configure_state(notify_state, name):
    ok = True
    try:
        modifier = unreal.new_object(unreal.RootMotionModifier_SkewWarp, outer=notify_state)
        notify_state.set_editor_property("root_motion_modifier", modifier)
        for prop, val in (
            ("warp_target_name", WARP_TARGET),
            ("warp_translation", True),
            ("warp_rotation", True),
            ("ignore_z_axis", True),
        ):
            try:
                modifier.set_editor_property(prop, val)
            except Exception as exc:  # noqa
                ok = False
                log("  %s: could not set %s (%s)" % (name, prop, exc))
        try:
            modifier.set_editor_property("rotation_type", unreal.MotionWarpRotationType.FACING)
        except Exception as exc:  # noqa
            ok = False
            log("  %s: could not set rotation_type (%s)" % (name, exc))
    except Exception as exc:  # noqa
        ok = False
        log("  %s: could not create Skew Warp modifier (%s)" % (name, exc))
    return ok


def main():
    montage = unreal.EditorAssetLibrary.load_asset(MONTAGE_PATH)
    if montage is None:
        log("ERROR: montage not found: " + MONTAGE_PATH)
        return
    if not hasattr(unreal, "AnimNotifyState_MotionWarping"):
        log("ERROR: AnimNotifyState_MotionWarping class missing - is the Motion Warping plugin enabled?")
        return

    segments = read_segments(montage)

    # rebuild the Lunge track
    try:
        existing = list(unreal.AnimationLibrary.get_animation_notify_track_names(montage))
    except Exception:
        existing = []
    if TRACK_NAME in [str(n) for n in existing]:
        unreal.AnimationLibrary.remove_animation_notify_track(montage, TRACK_NAME)
        log("Removed existing '%s' track" % TRACK_NAME)
    unreal.AnimationLibrary.add_animation_notify_track(montage, TRACK_NAME, unreal.LinearColor(1.0, 0.5, 0.1, 1.0))

    summary = []
    for idx, clip_name in enumerate(CLIP_NAMES):
        clip = find_clip(clip_name)
        if clip is None:
            log("ERROR: clip not found: " + clip_name)
            continue
        try:
            length, times, pts = root_xy_samples(clip)
        except Exception as exc:  # noqa
            log("ERROR sampling %s (%s). Is the root bone named '%s'?" % (clip_name, exc, ROOT_BONE))
            continue
        win = lunge_window(times, pts)
        if win is None:
            log("%s: no root travel found (root motion may be disabled/flat) - skipped" % clip_name)
            continue
        c_start, c_end, travel = win
        c_start = max(0.0, c_start - PAD_SECONDS)
        c_end = min(length, c_end + PAD_SECONDS)

        if segments and idx < len(segments):
            seg_start, seg_clip_start, rate = segments[idx]
        else:
            seg_start = FALLBACK_SEGMENT_STARTS[idx]
            seg_clip_start, rate = 0.0, 1.0
        rate = rate if rate else 1.0
        m_start = seg_start + (c_start - seg_clip_start) / rate
        m_dur = (c_end - c_start) / rate

        # add_animation_notify_state_event returns the created AnimNotifyState itself.
        state = unreal.AnimationLibrary.add_animation_notify_state_event(
            montage, TRACK_NAME, m_start, m_dur, unreal.AnimNotifyState_MotionWarping)
        ok = configure_state(state, clip_name) if state else False
        line = "%s: lunge window clip %.3f-%.3fs -> montage %.3f +%.3fs, root travel %.1f cm%s" % (
            clip_name, c_start, c_end, m_start, m_dur, travel, "" if ok else "  (CHECK PROPERTIES BY HAND)")
        log(line)
        summary.append(line)

    unreal.EditorAssetLibrary.save_loaded_asset(montage)
    log("Saved " + MONTAGE_PATH)
    log("Set LungeExpectedReach on the Sword combo ability from the root travel numbers above.")


try:
    main()
except Exception as exc:  # noqa
    log("FATAL: %s" % exc)
finally:
    flush_log()
