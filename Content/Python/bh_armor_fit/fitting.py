"""Fit Armor: build each armor set on each body type and harvest the fitted meshes.

One MetaHuman build per (set, body type): every resizable outfit of the set is equipped at once and
each comes out as its own mesh. Pieces that need rigging are rigged first.
"""
import time

from . import assets, checks, config, harvest, meshes, metahuman, outfits, rigging
from . import report as rpt


def fit_armor(sets=None, body_types=None, dry_run=False, force=False, reuse_builds=False):
    """Fit the given sets (default all) to the given body types (default all).

    Up-to-date pieces are skipped unless `force`. A dry run reports what would be built or skipped.
    With `reuse_builds` no MetaHuman build runs: the finals are re-harvested from the outfit meshes
    an earlier build left in the body type's Built_Fit folder.
    """
    pieces = config.load_pieces()
    selected_sets = [s for s in config.set_names(pieces) if sets is None or s in sets]
    bodies = [b for b in config.load_body_types() if body_types is None or b.name in body_types]
    run = rpt.Report("fit_armor", dry_run)
    if not selected_sets or not bodies:
        rpt.warn(config.LOG_FIT, "Nothing matches sets=%s body_types=%s." % (sets, body_types))
        return run.finish(config.LOG_FIT)
    checks.preflight()
    rig_names = [p.name for p in pieces if p.set_name in selected_sets and p.rigged_path]
    rigging.rig_into(run, rig_names, dry_run, force)
    jobs = [(s, b) for s in selected_sets for b in bodies]
    with rpt.Progress(len(jobs), "Fitting armor") as progress:
        for number, (set_name, body) in enumerate(jobs, 1):
            if progress.cancelled:
                run.add("%s/%s" % (set_name, body.name), "SKIP", "cancelled")
                break
            progress.step("%s -> %s" % (set_name, body.name))
            label = "%d/%d %s -> %s" % (number, len(jobs), set_name, body.name)
            _fit_set(config.pieces_of_set(pieces, set_name), body, label, run, dry_run, force, reuse_builds)
    return run.finish(config.LOG_FIT)


def _fit_set(set_pieces, body, label, run, dry_run, force, reuse_builds):
    groups = config.fit_groups(set_pieces)
    set_name = set_pieces[0].set_name
    if not groups:
        run.add("%s/%s" % (set_name, body.name), "SKIP", "no resizable pieces (rigid pieces are reused)")
        rpt.log(config.LOG_FIT, "%s ... no resizable pieces, skipped" % label)
        return
    group_outfits = {fit_id: outfits.OutfitSet(group[0]) for fit_id, group in groups.items()}
    if not force and _all_up_to_date(groups, group_outfits, body):
        for piece in (p for group in groups.values() for p in group):
            run.add("%s/%s" % (piece.name, body.name), "SKIP", "up to date")
        rpt.log(config.LOG_FIT, "%s ... up to date, skipped" % label)
        return
    if dry_run:
        count = sum(len(g) for g in groups.values())
        needs = [
            path
            for path in dict.fromkeys((body.body_mesh_path, group_outfits[next(iter(groups))].source_body))
            if not assets.exists(path)
        ]
        note = "; needs first: " + ", ".join(needs) if needs else ""
        action = "re-harvest" if reuse_builds else "build"
        run.add("%s/%s" % (set_name, body.name), "PLAN", "would %s %d outfit(s) -> %d piece(s)%s" % (action, len(groups), count, note))
        rpt.log(config.LOG_FIT, "%s ... would %s %d outfit(s), harvest %d piece(s)%s" % (label, action, len(groups), count, note))
        return
    started = time.time()
    try:
        _build_and_harvest(groups, group_outfits, body, label, run, started, reuse_builds)
    except Exception as exc:  # log and continue with the next set
        run.add("%s/%s" % (set_name, body.name), "FAIL", str(exc))
        rpt.error(config.LOG_FIT, "%s ... FAIL: %s" % (label, exc))


def _all_up_to_date(groups, group_outfits, body):
    if not assets.exists(body.body_mesh_path):
        return False
    for fit_id, group in groups.items():
        outfit = group_outfits[fit_id]
        for piece in group:
            if assets.read_stamp(piece.final_path(body)) != harvest.piece_stamp(piece, body, outfit):
                return False
    return True


def _build_and_harvest(groups, group_outfits, body, label, run, started, reuse_builds):
    if not assets.exists(body.body_mesh_path):
        raise RuntimeError("body type %s is not built; run Build Body Types first" % body.name)
    first = next(iter(groups.values()))[0]
    if not assets.exists(first.source_body):
        raise RuntimeError("source body missing: " + first.source_body)
    if reuse_builds:
        build_root = harvest.find_build_root(body, first.set_name)
        rpt.log(config.LOG_FIT, "%s ... re-harvesting the build in %s" % (label, build_root))
    else:
        build_root = _build(groups, group_outfits, body, first.set_name, label)
        rpt.log(config.LOG_FIT, "%s ... build OK (%d s)" % (label, time.time() - started))
    candidates = harvest.built_outfit_meshes(body, build_root)
    skeleton = assets.load(config.CANONICAL_SKELETON)
    body_bones = meshes.local_bone_positions(meshes.read(assets.load(body.body_mesh_path)))
    harvested = 0
    for fit_id, group in groups.items():
        try:
            built = harvest.match_built_mesh(group[0].fit_source, candidates)
        except Exception as exc:  # one unmatched outfit must not stop the rest of the set
            for piece in group:
                run.add("%s/%s" % (piece.name, body.name), "FAIL", str(exc))
                rpt.error(config.LOG_FIT, "%s ... %s FAIL: %s" % (label, piece.name, exc))
            continue
        for piece in group:
            item = "%s/%s" % (piece.name, body.name)
            try:
                detail = harvest.harvest_piece(piece, built, body, group_outfits[fit_id], skeleton, body_bones)
            except Exception as exc:  # one bad piece must not stop the rest
                run.add(item, "FAIL", str(exc))
                rpt.error(config.LOG_FIT, "%s ... %s FAIL: %s" % (label, piece.name, exc))
                continue
            run.add(item, "OK", detail, round(time.time() - started))
            harvested += 1
    rpt.log(config.LOG_FIT, "%s ... %d/%d harvested" % (label, harvested, sum(len(g) for g in groups.values())))


def _build(groups, group_outfits, body, set_name, label):
    """Build the set's outfits on the body type into its own folder (so the set can be re-harvested); returns the folder."""
    wardrobe_items = [outfits.ensure(group_outfits[fit_id]) for fit_id in groups]
    mhc = assets.load(body.mhc_path)
    rpt.log(config.LOG_FIT, "%s ... building %d outfit(s) (locks the editor for a few minutes)" % (label, len(groups)))
    build_root = "%s/%s" % (body.fit_root, set_name)
    metahuman.open_for_edit(mhc)
    try:
        metahuman.select_outfits(mhc, wardrobe_items)
        metahuman.build(mhc, build_root)
    finally:
        metahuman.close_edit(mhc)
    return build_root
