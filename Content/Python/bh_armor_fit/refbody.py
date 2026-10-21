"""Scaled copies of garments authored on a body of a different size than the row's SourceBody.

The BaseClothing meshes were authored on a body ~4.5% shorter than SK_tal_nrw_base (every bone, X and Z, is ~0.955x), but
their rows name tal_nrw as SourceBody, so the cloth resize never moved them up and the chest flares at the shoulders.

The ideal fix is a uniform 95.5% copy of the tal_nrw body as the SourceBody. GeometryScript can scale a mesh but cannot edit
the reference-pose bones of a skeletal mesh, so that body cannot be written. Scaling the GARMENT by the inverse factor and
keeping tal_nrw as SourceBody is the same similarity transform (garment relative to body is unchanged), so that is what this
module does: the scaled garment is re-skinned on tal_nrw's bones (BodyWeights transfer + core-bone cleanup, exactly like
rigging.py) and written next to the other ArmorFit assets. The Fab original is never touched.

    make_scaled_garment(source, package, scale=None)   one garment (scale None = fitted from the bones)
    ensure_scaled()                                    every entry of SCALED (what fit_armor calls for BaseClothing)

measure_scale() reports the per-bone ratios, the fitted factor and the largest bone deviation after scaling, so the
result can be checked against the authoring skeleton (pelvis / neck / head / clavicle within a few mm to ~0.6 cm).
"""
import types

import unreal

from . import assets, config, meshes, rigging, weights
from . import report as rpt

SCALED_ROOT = config.ARMOR_FIT_ROOT + "/Scaled"
AUTHORING_BODY = "/Game/Fab/Equipment/Polyphoria/CharacterParts_HeavyArmour/MH_Creator/SK_tal_nrw_base"

# scaled garment package -> original garment package (the DT_BH_ArmorPieces rows point their sourceMesh at the key)
SCALED = {
    SCALED_ROOT + "/Basic_Cloth_Chest_scaled": "/Game/Fab/Equipment/BaseClothing/Basic_Cloth_Chest",
    SCALED_ROOT + "/Basic_Cloth_Pants_scaled": "/Game/Fab/Equipment/BaseClothing/Basic_Cloth_Pants",
    SCALED_ROOT + "/Basic_Cloth_Boots_scaled": "/Game/Fab/Equipment/BaseClothing/Basic_Cloth_Boots",
}
KEY_BONES = ("pelvis", "spine_03", "spine_05", "neck_01", "head", "clavicle_l", "upperarm_l", "lowerarm_l", "thigh_l", "calf_l", "foot_l")
ABSOLUTE_TOLERANCE_CM = 1.5  # fail when a key bone is farther than this from the authoring body after scaling
REPORT_TOLERANCE_CM = 0.6


def ref_positions(skeletal_mesh):
    """{bone name: component-space reference-pose position (cm)} for the key bones of a skeletal mesh."""
    comp = unreal.SkeletalMeshComponent()
    comp.set_skeletal_mesh_asset(skeletal_mesh)
    names = [str(comp.get_bone_name(i)) for i in range(comp.get_num_bones())]
    index = {n: i for i, n in enumerate(names)}
    cache = {}

    def component(i):
        if i not in cache:
            local = comp.get_ref_pose_transform(i)
            parent = str(comp.get_parent_bone(names[i]))
            cache[i] = local if parent in ("None", "") or parent not in index else local.multiply(component(index[parent]))
        return cache[i]

    return {n: component(index[n]).translation for n in KEY_BONES if n in index}


def measure_scale(garment, body):
    """(factor, ratios, worst deviation cm): least-squares factor that maps the garment's authoring bones onto the body's.

    Uses every coordinate of the key bones (X and Z; Y is ~0 for most). ratios = {bone: body z / garment z}.
    """
    g, b = ref_positions(garment), ref_positions(body)
    common = [n for n in KEY_BONES if n in g and n in b]
    if len(common) < 5:
        raise RuntimeError("only %d key bones shared between %s and %s" % (len(common), garment.get_name(), body.get_name()))
    num = sum(g[n].x * b[n].x + g[n].y * b[n].y + g[n].z * b[n].z for n in common)
    den = sum(g[n].x ** 2 + g[n].y ** 2 + g[n].z ** 2 for n in common)
    factor = num / den
    ratios = {n: (b[n].z / g[n].z if abs(g[n].z) > 1e-3 else 0.0) for n in common}
    worst = max((g[n] * factor - b[n]).length() for n in common)
    return factor, ratios, worst


def make_scaled_garment(source_package, package, scale=None, source_body=AUTHORING_BODY, force=False):
    """Write `package`: the garment scaled about the origin by `scale` and re-skinned on `source_body`'s bones.

    Returns a short description. Skips when the stamp (source, factor, body, input mtimes) matches. Raises when the
    scaled authoring bones are farther than ABSOLUTE_TOLERANCE_CM from the body's.
    """
    source = assets.load(source_package)
    body_asset = assets.load(source_body)
    factor, ratios, worst = measure_scale(source, body_asset)
    if scale is not None:
        factor = float(scale)
        g, b = ref_positions(source), ref_positions(body_asset)
        worst = max((g[n] * factor - b[n]).length() for n in g if n in b)
    stamp = assets.stamp_of(source_package, round(factor, 5), source_body, assets.input_stamp(source_package, source_body), "v1")
    if not force and assets.read_stamp(package) == stamp:
        return "up to date (x%.4f)" % factor
    detail = ", ".join("%s %.3f" % (n, r) for n, r in ratios.items())
    rpt.log(config.LOG_FIT, "%s: bone height ratios body/garment: %s" % (source.get_name(), detail))
    rpt.log(config.LOG_FIT, "%s: scale x%.4f, worst key-bone deviation after scaling %.2f cm" % (source.get_name(), factor, worst))
    if worst > ABSOLUTE_TOLERANCE_CM:
        raise RuntimeError("%s: after x%.4f the key bones are still %.2f cm off %s; refusing to write %s" % (source.get_name(), factor, worst, source_body, package))
    if worst > REPORT_TOLERANCE_CM:
        rpt.warn(config.LOG_FIT, "%s: key bones %.2f cm off after scaling (> %.1f); check the result" % (source.get_name(), worst, REPORT_TOLERANCE_CM))

    mesh = meshes.read(source)
    before = meshes.bounds(mesh)
    mesh = unreal.GeometryScript_MeshTransforms.transform_mesh(mesh, unreal.Transform(scale=unreal.Vector(factor, factor, factor)), True)
    mesh = meshes.copy_bones(meshes.read(body_asset), mesh)
    mesh = rigging._first(rigging._WEIGHTS.mesh_create_bone_weights(mesh, True))
    mesh = rigging._weight_from_body(mesh, types.SimpleNamespace(source_body=source_body, name=source.get_name()))
    mesh, _ = weights.clean(mesh, source.get_name())
    after = meshes.bounds(mesh)
    expected_top = before.max.z * factor
    if abs(after.max.z - expected_top) > 0.5:
        raise RuntimeError("scaled mesh top %.2f differs from %.2f x %.4f = %.2f" % (after.max.z, before.max.z, factor, expected_top))
    named = meshes.slots(source)
    skeleton = assets.load(config.CANONICAL_SKELETON)
    target = meshes.write_skeletal(mesh, package, skeleton, [m for _, m in named], [n for n, _ in named])
    assets.write_stamp(target, stamp)
    assets.save([package])
    return "x%.4f, top %.1f -> %.1f cm, key bones within %.2f cm of the authoring body" % (factor, before.max.z, after.max.z, worst)


def ensure_scaled(force=False):
    """Create or refresh every scaled garment of SCALED; returns {package: description}. Failures are logged and re-raised after all ran."""
    results, failures = {}, []
    for package, source in SCALED.items():
        try:
            results[package] = make_scaled_garment(source, package, force=force)
            rpt.log(config.LOG_FIT, "scaled garment %s ... %s" % (package.rsplit("/", 1)[-1], results[package]))
        except Exception as exc:  # noqa: BLE001
            failures.append("%s: %s" % (package, exc))
            rpt.error(config.LOG_FIT, "scaled garment %s FAIL: %s" % (package.rsplit("/", 1)[-1], exc))
    if failures:
        raise RuntimeError("scaled garment step failed: " + "; ".join(failures))
    return results
