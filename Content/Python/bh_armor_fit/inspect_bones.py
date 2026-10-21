"""Read-only: component-space reference-pose positions of key bones, source meshes vs bodies vs fitted meshes.

    py import importlib, bh_armor_fit.inspect_bones as b; importlib.reload(b); b.run()
"""
import unreal

KEY = ["pelvis", "spine_03", "spine_05", "neck_01", "head", "clavicle_l", "upperarm_l", "lowerarm_l", "hand_l", "thigh_l", "calf_l", "calf_twist_01_l", "foot_l", "ball_l", "foot_r"]
BOOTS = [
    ("BODY tal_nrw (source body)", "/Game/Fab/Equipment/Polyphoria/CharacterParts_HeavyArmour/MH_Creator/SK_tal_nrw_base"),
    ("BODY MaleNormal Built_Fit", "/Game/BlackwoodHollow/Characters/BodyTypes/MaleNormal/Built_Fit/MHC_MaleNormal/Body/SKM_MHC_MaleNormal_BodyMesh"),
    ("SRC BasicLeather Boots (SK_Mannequin)", "/Game/Fab/Equipment/BasicLeather/Boots"),
    ("SRC Platemail Boots (works)", "/Game/Fab/Equipment/5-piece-platemail-metahuman-rigged/Platemail_Boots"),
    ("FIT BasicLeather Boots", "/Game/BlackwoodHollow/Equipment/Armor/BasicLeather/SKM_BasicLeather_Boots_MaleNormal"),
    ("FIT Platemail Boots", "/Game/BlackwoodHollow/Equipment/Armor/Platemail/SKM_Platemail_Boots_MaleNormal"),
]
MESHES = [
    ("BODY tal_nrw (source body)", "/Game/Fab/Equipment/Polyphoria/CharacterParts_HeavyArmour/MH_Creator/SK_tal_nrw_base"),
    ("BODY MaleNormal Built_Fit", "/Game/BlackwoodHollow/Characters/BodyTypes/MaleNormal/Built_Fit/MHC_MaleNormal/Body/SKM_MHC_MaleNormal_BodyMesh"),
    ("SRC BaseClothing Chest", "/Game/Fab/Equipment/BaseClothing/Basic_Cloth_Chest"),
    ("SRC BaseClothing Pants", "/Game/Fab/Equipment/BaseClothing/Basic_Cloth_Pants"),
    ("SRC Starter Chest", "/Game/Fab/Equipment/StarterClothArmor/Cloth_Full_Rigged123"),
    ("SRC Platemail Chest (works)", "/Game/Fab/Equipment/5-piece-platemail-metahuman-rigged/Platemail_Chest"),
    ("FIT BaseClothing Chest", "/Game/BlackwoodHollow/Equipment/Armor/BaseClothing/SKM_BaseClothing_Chest_MaleNormal"),
    ("FIT BasicLeather Chest (works)", "/Game/BlackwoodHollow/Equipment/Armor/BasicLeather/SKM_BasicLeather_Chest_MaleNormal"),
]


def _find(path):
    asset = unreal.load_asset(path)
    if asset is None:
        # tolerate folder differences: search by name
        name = path.rsplit("/", 1)[1]
        hits = unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_class(unreal.TopLevelAssetPath("/Script/Engine", "SkeletalMesh"))
        for h in hits:
            if str(h.asset_name) == name:
                return h.get_asset()
    return asset


def _positions(mesh):
    comp = unreal.SkeletalMeshComponent()
    comp.set_skeletal_mesh_asset(mesh)
    names = [str(comp.get_bone_name(i)) for i in range(comp.get_num_bones())]
    index = {n: i for i, n in enumerate(names)}
    cache = {}

    def component(i):
        # component-space ref pose = local ref transform composed with the parent's component transform
        if i in cache:
            return cache[i]
        local = comp.get_ref_pose_transform(i)
        parent = comp.get_parent_bone(names[i])
        if str(parent) in ("None", "") or str(parent) not in index:
            result = local
        else:
            result = local.multiply(component(index[str(parent)]))
        cache[i] = result
        return result

    out = {}
    for key in KEY:
        match = next((n for n in names if n == key), None) or next((n for n in names if n.startswith(key)), None)
        if match:
            out[key] = (match, component(index[match]))
    return names, out


def run(meshes=None):
    for label, path in (meshes or MESHES):
        mesh = _find(path)
        if mesh is None:
            unreal.log_warning("[BH_Bones] %s missing: %s" % (label, path))
            continue
        try:
            names, pos = _positions(mesh)
        except Exception as error:  # noqa: BLE001
            unreal.log_warning("[BH_Bones] %s failed: %s" % (label, error))
            continue
        unreal.log("[BH_Bones] %s  (%d bones, first %s) %s" % (label, len(names), names[:2], mesh.get_path_name()))
        for key in KEY:
            if key in pos:
                n, tr = pos[key]; v = tr.translation; r = tr.rotation.rotator()
                unreal.log("[BH_Bones]     %-15s %-18s %7.1f %7.1f %7.1f   rot p%6.1f y%6.1f r%6.1f" % (key, n, v.x, v.y, v.z, r.pitch, r.yaw, r.roll))
            else:
                unreal.log("[BH_Bones]     %-11s (missing)" % key)
