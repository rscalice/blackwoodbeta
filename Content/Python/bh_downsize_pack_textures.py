"""Downsize the SOURCE data of oversized textures in newly imported Fab packs.

Mechanism: unreal.TextureAssetActionsBlueprintFns.resize_texture_source(tex, N)
(engine: UnrealEd/Public/TextureAssetActionsBP.h). It resamples the bitmap stored
in the .uasset to <= N and auto-adjusts LODBias, so the in-game size is unchanged
and the file (and LFS upload) shrinks. Power-of-two sources stay power-of-two.

Run in the editor Python console / Output Log (Cmd: py ...):
  py "<project>/Content/Python/bh_downsize_pack_textures.py"            (DRY RUN, default)
To really resize, set the env-style flag first:
  py "<project>/Content/Python/bh_downsize_pack_textures.py" --apply

Dry run lists every texture whose source is > MAX_SIZE with file size and an
estimated saving (file size * (new_pixels/old_pixels)). Apply mode resizes,
saves ONLY the changed assets, and prints before/after file sizes.
Edit PACKS to limit scope. Restart is not needed.
"""
import os
import sys
import unreal

MAX_SIZE = 2048
# Every pack folder under /Game/Fab, except:
#  - VFX packs: their big textures are flipbooks (grids of animation frames); halving blurs every frame.
#  - UI/icon packs: icons and UI art must stay crisp.
#  - MetaHuman: skin/groom textures drive the character close-ups and the inventory preview.
EXCLUDE = {
    "DEKO_VFX", "M5VFXVOL2", "TorchFire", "Vefects", "SlashandHitVFX", "RPGEnvironmentVFX",
    "__AoeVFX", "Free_Spells", "UIEffectLab", "RealisticBlood", "Generic_Item_Destruction_FX",
    "ErodingCircle", "FreePostProcessV1_1",
    "100+Dark_Fantasy_Potion_Icons", "50+Dark_Fantasy_Shield_Icons", "FantasyIconsMegapack",
    "RPGClassIcons", "ui_medieval_free", "EasyInputPrompts",
    "MetaHuman",
}
# Every pack folder under /Game/Fab except EXCLUDE (Robert, 2026-10-21: all packs).
_fab_dir = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir()), "Fab")
PACKS = sorted(d for d in os.listdir(_fab_dir)
               if os.path.isdir(os.path.join(_fab_dir, d)) and d not in EXCLUDE)
APPLY = "--apply" in sys.argv or globals().get("BH_APPLY", False)

reg = unreal.AssetRegistryHelpers.get_asset_registry()
proj = unreal.Paths.project_content_dir()


def disk_path(pkg):
    return os.path.join(proj, pkg.replace("/Game/", "", 1) + ".uasset")


def fsize(pkg):
    p = disk_path(pkg)
    return os.path.getsize(p) if os.path.exists(p) else 0


todo = []
for pack in PACKS:
    for a in reg.get_assets_by_path("/Game/Fab/" + pack, recursive=True):
        if str(a.asset_class_path.asset_name) not in ("Texture2D", "TextureCube", "VolumeTexture"):
            continue
        if str(a.asset_class_path.asset_name) != "Texture2D":
            continue
        pkg = str(a.package_name)
        dim = str(a.get_tag_value("Dimensions") or "")  # source dimensions e.g. "8192x8192"
        try:
            w, h = [int(v) for v in dim.lower().split("x")[:2]]
        except Exception:
            tex = a.get_asset()
            w, h = tex.blueprint_get_size_x(), tex.blueprint_get_size_y()
        if max(w, h) > MAX_SIZE:
            todo.append((pack, pkg, w, h, fsize(pkg)))

total_before = sum(t[4] for t in todo)
est_after = 0
for pack, pkg, w, h, sz in todo:
    scale = (MAX_SIZE / float(max(w, h))) ** 2
    est_after += sz * scale
    unreal.log("[%s] %s  %dx%d  %.1f MB -> ~%.1f MB" % (pack, pkg, w, h, sz / 1048576.0, sz * scale / 1048576.0))
unreal.log("== %d textures > %d. Now %.0f MB, est after %.0f MB, est saving %.0f MB ==" % (
    len(todo), MAX_SIZE, total_before / 1048576.0, est_after / 1048576.0, (total_before - est_after) / 1048576.0))

if not APPLY:
    unreal.log("DRY RUN only. Re-run with --apply (or set BH_APPLY = True) to resize and save.")
else:
    changed = []
    for pack, pkg, w, h, sz in todo:
        tex = unreal.load_asset(pkg)
        r = unreal.TextureAssetActionsBlueprintFns.resize_texture_source(tex, MAX_SIZE)
        unreal.log("%s -> %s" % (pkg, r))
        if r == unreal.TextureAssetActionsBlueprintReturn.SUCCESS:
            unreal.EditorAssetLibrary.save_loaded_asset(tex, only_if_is_dirty=False)
            changed.append((pkg, sz))
    after = 0
    for pkg, sz in changed:
        n = fsize(pkg)
        after += n
        unreal.log("%s  %.1f MB -> %.1f MB" % (pkg, sz / 1048576.0, n / 1048576.0))
    unreal.log("== resized %d textures; %.0f MB -> %.0f MB ==" % (
        len(changed), sum(s for _, s in changed) / 1048576.0, after / 1048576.0))
