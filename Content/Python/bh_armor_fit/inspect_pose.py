"""Read-only, run DURING PIE: live bone positions/scales on the visual body vs each armor piece (Leader Pose followers).

    py import importlib, bh_armor_fit.inspect_pose as p; importlib.reload(p); p.run()
"""
import unreal

BONES = ["lowerarm_l", "lowerarm_twist_01_l", "lowerarm_twist_02_l", "hand_l", "middle_03_l",
         "lowerarm_r", "lowerarm_twist_01_r", "lowerarm_twist_02_r", "hand_r", "middle_03_r",
         "calf_l", "calf_twist_01_l", "calf_twist_02_l", "foot_l",
         "calf_r", "calf_twist_01_r", "calf_twist_02_r", "foot_r"]
LOG = "[BH_Pose]"


def _world():
    try:
        return unreal.UnrealEditorSubsystem().get_game_world()
    except Exception:  # noqa: BLE001
        return unreal.EditorLevelLibrary.get_game_world()


def _fmt(v):
    return "(%7.1f %7.1f %7.1f)" % (v.x, v.y, v.z)


def run():
    world = _world()
    if world is None:
        unreal.log_warning("%s no PIE world: start PIE first" % LOG)
        return
    actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)
    for actor in actors:
        comps = actor.get_components_by_class(unreal.SkeletalMeshComponent)
        followers = [c for c in comps if c.get_editor_property("leader_pose_component") is not None] if comps else []
        if not followers:
            continue
        leader = followers[0].get_editor_property("leader_pose_component")
        unreal.log("%s actor %s  leader %s (%s)  LOD %s" % (LOG, actor.get_name(), leader.get_name(),
                   leader.get_skeletal_mesh_asset().get_name() if leader.get_skeletal_mesh_asset() else "?",
                   getattr(leader, "get_predicted_lod_level", lambda: "?")()))
        for c in [leader] + followers:
            mesh = c.get_skeletal_mesh_asset()
            unreal.log("%s  -- %s  mesh=%s visible=%s hidden=%s bounds=%s" % (
                LOG, c.get_name(), mesh.get_name() if mesh else None, c.is_visible(), c.get_editor_property("hidden_in_game"),
                _fmt(c.bounds.box_extent) if hasattr(c, "bounds") else "?"))
            for b in BONES:
                if c.get_bone_index(b) < 0:
                    continue
                t = c.get_socket_transform(b, unreal.RelativeTransformSpace.RTS_WORLD)
                s = t.scale3d
                flag = "  <-- SCALE" if min(abs(s.x), abs(s.y), abs(s.z)) < 0.2 else ""
                unreal.log("%s     %-20s %s scale %.2f %.2f %.2f%s" % (LOG, b, _fmt(t.translation), s.x, s.y, s.z, flag))
