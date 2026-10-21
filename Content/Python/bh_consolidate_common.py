"""bh_consolidate_common.py - retire /Game/Skeletons/MetaHuman/Built/Common.

For every asset in OLD that has a same-name, same-class twin in NEW, repoint all
users to the twin (Unreal "Replace References" = consolidate). Assets with no twin,
or used by anything outside /Game (engine/plugin content), are SKIPPED and listed.

Run from the editor:  Tools > Execute Python Script... > pick this file.
DRY_RUN = True only reports. Set it to False and run again to apply.
Close all asset editors first. Save nothing outside /Game.
"""
import unreal

DRY_RUN = False
OLD = "/Game/Skeletons/MetaHuman/Built/Common"
NEW = "/Game/MetaHumans/Common"
TAG = "[BH_Consolidate]"

ar = unreal.AssetRegistryHelpers.get_asset_registry()
eal = unreal.EditorAssetLibrary


def log(msg):
    unreal.log(f"{TAG} {msg}")


def referencers(pkg):
    opts = unreal.AssetRegistryDependencyOptions(
        include_soft_package_references=True,
        include_hard_package_references=True,
        include_searchable_names=False,
        include_soft_management_references=False,
        include_hard_management_references=False)
    return [str(p) for p in (ar.get_referencers(pkg, opts) or [])]


def outside(refs, root):
    """Referencers that live outside `root` (the folder being retired)."""
    return [r for r in refs if not r.startswith(root + "/")]


def class_of(path):
    d = eal.find_asset_data(path)
    return str(d.asset_class_path.asset_name) if d and d.is_valid() else None


def plan():
    olds = [p.split(".")[0] for p in eal.list_assets(OLD, recursive=True, include_folder=False)]
    todo, skipped = [], []
    for old in olds:
        refs = outside(referencers(old), OLD)
        if not refs:
            continue  # nothing uses it; it is simply deleted at the end
        twin = NEW + old[len(OLD):]
        if not eal.does_asset_exist(twin) or class_of(twin) != class_of(old):
            skipped.append((old, "no matching twin in NEW", refs))
            continue
        bad = [r for r in refs if not r.startswith("/Game/")]
        if bad:
            skipped.append((old, "used by engine/plugin content", bad))
            continue
        todo.append((old, twin, refs))
    # skeleton first, so meshes/anims keep a valid skeleton throughout
    todo.sort(key=lambda t: 0 if class_of(t[0]) == "Skeleton" else 1)
    return todo, skipped


RETIRE = "/Game/Skeletons/MetaHuman"   # whole folder to remove once unused


def cleanup():
    """Delete every asset under RETIRE that nothing outside RETIRE uses."""
    keep, gone = [], []
    for p in eal.list_assets(RETIRE, recursive=True, include_folder=False):
        p = p.split(".")[0]
        refs = outside(referencers(p), RETIRE)
        if refs:
            keep.append((p, refs))
        else:
            gone.append(p)
    for p, refs in keep:
        log(f"KEEP (still used) {p}  <- {', '.join(refs[:4])}")
    log(f"{'would delete' if DRY_RUN else 'deleting'} {len(gone)} unused assets under {RETIRE}")
    if not DRY_RUN:
        for p in gone:
            if not eal.delete_asset(p):
                log(f"  delete FAILED: {p}")
        if not keep:
            eal.delete_directory(RETIRE)
    return keep, gone



def main():
    log(f"===== {'DRY RUN' if DRY_RUN else 'APPLY'}  {OLD} -> {NEW} =====")
    if not eal.does_directory_exist(NEW):
        log(f"ABORT: {NEW} not found")
        return
    todo, skipped = plan()
    for old, why, refs in skipped:
        log(f"SKIP {old}: {why}  <- {', '.join(refs[:4])}")
    log(f"{len(todo)} assets to repoint, {len(skipped)} skipped")
    touched = set()
    for old, twin, refs in todo:
        log(f"{'would repoint' if DRY_RUN else 'repoint'} {old} -> {twin}  ({len(refs)} users)")
        touched.update(refs)
        if not DRY_RUN:
            if not eal.consolidate_assets(eal.load_asset(twin), [eal.load_asset(old)]):
                log(f"  consolidate FAILED for {old}")
    if not DRY_RUN:
        for pkg in sorted(touched):
            if pkg.startswith("/Game/") and not pkg.startswith(RETIRE + "/"):
                eal.save_asset(pkg, only_if_is_dirty=True)
        log(f"saved referencing packages (/Game only)")
    cleanup()
    log("===== done. DRY RUN changed nothing." if DRY_RUN
        else "===== done. Now: Fix Up Redirectors on /Game, then compile + PIE.")


main()