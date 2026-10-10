"""Apply To Items: write the fitted meshes into the armor item Blueprints' Visual (FBH_ArmorVisual)."""
import unreal

from . import assets, checks, config
from . import report as rpt


def apply_to_items(dry_run=False):
    """Point every piece's TargetItem at its fitted skeletal mesh.

    Sets the skeletal mesh, clears the static mesh and the stale hidden-slot and material
    overrides, and sets bHidesHair for helms. Items are written to the first body type (items hold a
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
        outcome = _apply_one(piece, mesh_package, dry_run)
        run.add(piece.name, outcome, mesh_package)
        rpt.log(config.LOG_FIT, "%s ... %s" % (label, outcome))
        if outcome == "OK":
            changed.append(piece.target_item)
    if changed:
        assets.save(changed)
    return run.finish(config.LOG_FIT)


def _apply_one(piece, mesh_package, dry_run):
    blueprint = assets.load(piece.target_item)
    defaults = unreal.get_default_object(blueprint.generated_class())
    visual = defaults.get_editor_property("visual")
    mesh = assets.load(mesh_package)
    wants_hair_hidden = piece.slot == "Helm"
    current = visual.get_editor_property("skeletal_mesh")
    if (
        current is not None
        and current.get_path_name() == mesh.get_path_name()
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
    defaults.modify(True)
    defaults.set_editor_property("visual", visual)
    return "OK"
