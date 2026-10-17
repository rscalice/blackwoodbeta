"""Apply To Items: write the fitted meshes into the armor item Blueprints' Visual (FBH_ArmorVisual)."""
import unreal

from . import assets, checks, config
from . import report as rpt


def apply_to_items(dry_run=False):
    """Point every piece's TargetItem at its fitted skeletal mesh.

    Sets the skeletal mesh and the body mask texture (T_BHMask_*, when the fit wrote one), clears the static mesh and the
    stale hidden-slot and material overrides, and sets bHidesHair for helms. The starting outfit (BaseClothing, no item)
    is configured in Config/DefaultGame.ini + UBH_RPGSettings with the same deterministic mesh and mask paths. Items are written to the first body type (items hold a
    single visual for now).
    """
    pieces = [p for p in config.load_pieces() if p.target_item]
    body = config.load_body_types()[0]
    run = rpt.Report("apply_to_items", dry_run)
    checks.preflight()
    changed = []
    for number, piece in enumerate(pieces, 1):
        label = "%d/%d %s -> %s" % (number, len(pieces), piece.name, piece.target_item.rsplit("/", 1)[-1])
        mesh_package = piece.final_path(body)
        if not assets.exists(mesh_package):
            run.add(piece.name, "FAIL", "mesh not built yet: " + mesh_package)
            rpt.error(config.LOG_FIT, "%s ... mesh not built yet (%s)" % (label, mesh_package))
            continue
        outcome = _apply_one(piece, mesh_package, body, dry_run)
        run.add(piece.name, outcome, mesh_package)
        rpt.log(config.LOG_FIT, "%s ... %s" % (label, outcome))
        if outcome == "OK":
            changed.append(piece.target_item)
    if changed:
        assets.save(changed)
    return run.finish(config.LOG_FIT)


def _apply_one(piece, mesh_package, body, dry_run):
    blueprint = assets.load(piece.target_item)
    defaults = unreal.get_default_object(blueprint.generated_class())
    visual = defaults.get_editor_property("visual")
    mesh = assets.load(mesh_package)
    wants_hair_hidden = piece.slot == "Helm"
    mask_package = piece.mask_path(body)
    mask = assets.load(mask_package) if mask_package and assets.exists(mask_package) else None
    current = visual.get_editor_property("skeletal_mesh")
    current_mask = visual.get_editor_property("body_mask")
    if (
        current is not None
        and current.get_path_name() == mesh.get_path_name()
        and (current_mask.get_path_name() if current_mask else "") == (mask.get_path_name() if mask else "")
        and visual.get_editor_property("static_mesh") is None
        and not visual.get_editor_property("hidden_material_slots")
        and not visual.get_editor_property("material_overrides")
        and visual.get_editor_property("hides_hair") == wants_hair_hidden
    ):
        return "SKIP"
    if dry_run:
        return "PLAN"
    visual.set_editor_property("skeletal_mesh", mesh)
    visual.set_editor_property("static_mesh", None)
    visual.set_editor_property("hidden_material_slots", [])
    visual.set_editor_property("material_overrides", [])
    visual.set_editor_property("hides_hair", wants_hair_hidden)
    visual.set_editor_property("body_mask", mask)  # None for pieces without a mask (helms) clears a stale one
    defaults.modify(True)
    defaults.set_editor_property("visual", visual)
    return "OK"
