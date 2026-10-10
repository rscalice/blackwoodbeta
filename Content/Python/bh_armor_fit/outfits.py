"""Per-group cloth asset, outfit asset and wardrobe item, made by repointing the wired template set.

Python cannot wire dataflow terminal nodes, so every set is a duplicate of the template chain
(DF_ca -> CA -> OA -> WI). The outfit dataflow DF_ResizableOutfit_Template is shared, not copied.
"""
import re

import unreal

from . import assets, config

_SKELETAL_MESH_NODE = "SkeletalMeshImport"
_BODY_PARTS = re.compile(r'SourceBodyParts=\("[^"]*"\)')


class OutfitSet:
    """Package names of one group's cloth/outfit/wardrobe chain."""

    def __init__(self, first_piece):
        self.fit_id = first_piece.fit_id
        folder = first_piece.outfit_dir
        self.df_ca = "%s/ClothAssets/DF_ca_%s" % (folder, self.fit_id)
        self.ca = "%s/ClothAssets/CA_%s" % (folder, self.fit_id)
        self.oa = "%s/OA_%s/OA_%s" % (folder, self.fit_id, self.fit_id)
        self.wi = "%s/WI_%s" % (folder, self.fit_id)
        self.source_mesh = first_piece.fit_source
        self.source_body = first_piece.source_body

    @property
    def packages(self):
        return [self.df_ca, self.ca, self.oa, self.wi]

    def expected_stamp(self):
        return assets.stamp_of(
            self.source_mesh,
            self.source_body,
            assets.input_stamp(
                self.source_mesh, self.source_body, config.TEMPLATE_DF_CA, config.TEMPLATE_CA, config.TEMPLATE_OA
            ),
        )


def ensure(outfit):
    """Create or refresh the chain for `outfit`; returns the wardrobe item package.

    Does nothing when the OA's stamp already matches its inputs.
    """
    if assets.read_stamp(outfit.oa) == outfit.expected_stamp():
        return outfit.wi
    for template, target in (
        (config.TEMPLATE_DF_CA, outfit.df_ca),
        (config.TEMPLATE_CA, outfit.ca),
        (config.TEMPLATE_OA, outfit.oa),
        (config.TEMPLATE_WI, outfit.wi),
    ):
        if not assets.exists(target):
            assets.duplicate(template, target)
    _point_cloth_dataflow(outfit)
    _point_cloth_asset(outfit)
    _point_outfit_asset(outfit)
    _point_wardrobe_item(outfit)
    cloth = assets.load(outfit.ca)
    outfit_asset = assets.load(outfit.oa)
    unreal.DataflowBlueprintLibrary.regenerate_asset_from_dataflow(cloth, False)
    unreal.DataflowBlueprintLibrary.regenerate_asset_from_dataflow(outfit_asset, False)
    assets.write_stamp(outfit_asset, outfit.expected_stamp())
    assets.save(outfit.packages)
    return outfit.wi


def _swap(text, old_package, new_package):
    """Replace a package path everywhere, object-path form first so the asset name changes too."""
    text = text.replace(assets.object_path(old_package), assets.object_path(new_package))
    return text.replace(old_package, new_package)


def _point_cloth_dataflow(outfit):
    graph = assets.load(outfit.df_ca)
    value = "/Script/Engine.SkeletalMesh'%s'" % assets.object_path(outfit.source_mesh)
    if not unreal.DataflowEditorBlueprintLibrary.set_dataflow_node_property(
        graph, _SKELETAL_MESH_NODE, "SkeletalMesh", value
    ):
        raise RuntimeError("Could not set the source mesh on " + outfit.df_ca)
    graph.modify(True)


def _point_cloth_asset(outfit):
    cloth = assets.load(outfit.ca)
    instance = cloth.get_editor_property("dataflow_instance")
    text = _swap(instance.export_text(), config.TEMPLATE_DF_CA, outfit.df_ca)
    instance.import_text(_swap(text, config.TEMPLATE_CA, outfit.ca))
    cloth.modify(True)
    cloth.set_editor_property("dataflow_instance", instance)


def _point_outfit_asset(outfit):
    outfit_asset = assets.load(outfit.oa)
    instance = outfit_asset.get_editor_property("dataflow_instance")
    text = _swap(instance.export_text(), config.TEMPLATE_CA, outfit.ca)
    text = _swap(text, config.TEMPLATE_OA, outfit.oa)
    body = "SourceBodyParts=(\"/Script/Engine.SkeletalMesh'%s'\")" % assets.object_path(outfit.source_body)
    text, replaced = _BODY_PARTS.subn(body, text)
    if replaced != 1:
        raise RuntimeError("Outfit dataflow text has no SourceBodyParts entry: " + outfit.oa)
    instance.import_text(text)
    outfit_asset.modify(True)
    outfit_asset.set_editor_property("dataflow_instance", instance)


def _point_wardrobe_item(outfit):
    item = assets.load(outfit.wi)
    principal = item.get_editor_property("principal_asset")
    path = assets.object_path(outfit.oa)
    principal.import_text('(Asset="%s",AssetIdentifier="%s")' % (path, path))
    item.modify(True)
    item.set_editor_property("principal_asset", principal)
