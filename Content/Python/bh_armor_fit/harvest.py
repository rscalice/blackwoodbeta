"""Harvest: map the meshes a set build produced back to its pieces and write the final armor meshes.

A MetaHuman build writes one SkeletalMesh per equipped outfit (<Char>_Outfits, <Char>_Outfits_2 ...)
in no guaranteed order. It keeps the sections in source order but renames the material slots (to the
material's name, or "None") and ignores the materials set on the cloth asset. So each built mesh is
matched to its group by section count and triangle count, sections are mapped by index, and the
harvested copy gets the source's slot names back before the piece's materials are applied.

Every harvested mesh is re-skinned to core bones (weights.clean) and then pushed out of the body skin
(clearance.push_out) before it is written.
"""
from . import assets, clearance, meshes, weights

SANITY_BOUNDS_CM = 10.0
# Vertex counts are not comparable: the source and built meshes split vertices at seams differently
# (the platemail chest reads 19,910 vs 31,932 for the same fit), so checks use triangles and bounds.
# The build may also drop a few degenerate triangles (platemail chest: 37,020 -> 36,961).
SANITY_TRIANGLE_TOLERANCE = 0.005


def built_outfit_meshes(body, build_root):
    """Package paths of the outfit meshes a build into `build_root` produced."""
    folder = "%s/MHC_%s/Clothing" % (build_root, body.name)
    return [p for p in assets.list_folder(folder) if "_Outfits" in p.rsplit("/", 1)[-1]]


def find_build_root(body, set_name):
    """The build folder that holds the outfit meshes of `set_name` for re-harvesting without a build.

    The per-set folder (<fit_root>/<set>) is preferred; the flat <fit_root> is the legacy location.
    """
    for root in ("%s/%s" % (body.fit_root, set_name), body.fit_root):
        if built_outfit_meshes(body, root):
            return root
    raise RuntimeError("no built outfit meshes for %s under %s" % (set_name, body.fit_root))


def match_built_mesh(group_source, candidates):
    """The built outfit mesh that belongs to a group: same section count and (nearly) the same triangle count.

    Not slot names (the build renames them) and not vertex count (the build splits vertices at seams
    differently from the source); section order is preserved and triangles are, up to a few degenerate ones.
    """
    source = assets.load(group_source)
    expected = (len(meshes.slots(source)), meshes.triangle_count(meshes.read(source)))
    found = {}
    for candidate in candidates:
        built = assets.load(candidate)
        found[candidate] = (len(meshes.slots(built)), meshes.triangle_count(meshes.read(built)))
    pool = [c for c, (sections, tris) in found.items() if sections == expected[0] and _tris_match(tris, expected[1])]
    if not pool:
        listing = {c.rsplit("/", 1)[-1]: key for c, key in found.items()}
        raise RuntimeError("no built outfit mesh with %d sections / %d tris (built: %s)" % (expected + (listing,)))
    pool.sort(key=lambda c: (abs(found[c][1] - expected[1]), -assets.disk_mtime(c)))
    return pool[0]


def _tris_match(got, want):
    return abs(got - want) <= SANITY_TRIANGLE_TOLERANCE * want


def piece_stamp(piece, body, outfit):
    return assets.stamp_of(
        outfit.expected_stamp(),
        weights.SIGNATURE,
        clearance.SIGNATURE,
        assets.read_stamp(body.body_mesh_path),
        piece.sections_to_keep,
        sorted(piece.material_overrides.items()),
        assets.input_stamp(*piece.material_overrides.values()),
    )


def harvest_piece(piece, built_package, body, outfit, skeleton, body_bones):
    """Write the final mesh for one piece; returns a short detail string. Raises when a sanity check fails.

    `body_bones` is local_bone_positions() of the body type's mesh: the final must share its bind pose.
    """
    source = assets.load(piece.fit_source)
    built = assets.load(built_package)
    source_slots = meshes.slots(source)
    built_slots = meshes.slots(built)
    final = piece.final_path(body)
    if piece.sections_to_keep:
        target, expected = _write_split(piece, source, source_slots, built, built_slots, final, skeleton, body)
    else:
        target, expected = _write_whole(piece, source, source_slots, built_package, built_slots, final, skeleton, body)
    unmatched = meshes.apply_material_overrides(target, piece.material_overrides)
    if unmatched:
        raise RuntimeError("material override slot(s) %s not on %s" % (unmatched, final))
    detail = _sanity_check(target, expected, body_bones)
    assets.write_stamp(target, piece_stamp(piece, body, outfit))
    assets.save([final])
    return detail


def _write_whole(piece, source, source_slots, built_package, built_slots, final, skeleton, body):
    expected = meshes.read(source)
    mesh, _ = weights.clean(meshes.read(assets.load(built_package)), piece.name)
    mesh, _ = clearance.push_out(mesh, body.body_mesh_path, piece.name)
    target = meshes.write_skeletal(mesh, final, skeleton, [m for _, m in built_slots], [n for n, _ in source_slots])
    return target, expected


def _write_split(piece, source, source_slots, built, built_slots, final, skeleton, body):
    """Keep only the listed sections; built sections are in source order, so indices carry over."""
    keep = list(piece.sections_to_keep)
    expected = meshes.keep_sections(meshes.read(source), keep, len(source_slots))
    mesh = meshes.keep_sections(meshes.read(built), keep, len(built_slots))
    mesh, materials = meshes.compact_sections(mesh, [m for _, m in built_slots])
    names = [source_slots[i][0] for i in sorted(keep)]
    mesh, _ = weights.clean(mesh, piece.name)
    mesh, _ = clearance.push_out(mesh, body.body_mesh_path, piece.name)
    return meshes.write_skeletal(mesh, final, skeleton, materials, names), expected


def _sanity_check(target, expected, body_bones):
    """Triangles and bounds close to the source's, the body's bind pose, and core-bone weights only."""
    result = meshes.read(target)
    pose_delta = meshes.bind_pose_delta(meshes.local_bone_positions(result), body_bones)
    if pose_delta > meshes.BIND_POSE_TOLERANCE_CM:
        raise RuntimeError("bind pose differs from the body's by %.3f cm" % pose_delta)
    audit = weights.audit(result)
    if audit["missing"] or audit["non_core"] or audit["max_influences"] > weights.MAX_INFLUENCES:
        raise RuntimeError("weights not clean: %s" % audit)
    tris, want_tris = meshes.triangle_count(result), meshes.triangle_count(expected)
    if not _tris_match(tris, want_tris):
        raise RuntimeError("triangle count %d differs from source %d by more than %.1f%%" % (tris, want_tris, SANITY_TRIANGLE_TOLERANCE * 100))
    if not meshes.bounds_within(meshes.bounds(result), meshes.bounds(expected), SANITY_BOUNDS_CM):
        raise RuntimeError("bounds moved more than %s cm from the source" % SANITY_BOUNDS_CM)
    return "%d tris (source %d), bounds within %s cm, bind pose delta %.4f cm, core-bone weights only" % (
        tris, want_tris, SANITY_BOUNDS_CM, pose_delta
    )
