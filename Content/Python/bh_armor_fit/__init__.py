"""Blackwood Hollow armor fit tool.

Commands (also in the editor menu Blackwood Hollow > Armor Fit):
    build_body_types(names=None, dry_run=False)
    rig_static_pieces(pieces=None, dry_run=False, force=False)
    fit_armor(sets=None, body_types=None, dry_run=False, force=False, reuse_builds=False, posed_check=None)
    apply_to_items(dry_run=False)
    open_report()
    apply_body_material()   (masked skin on every built body; build_body_types does it automatically)
Config lives in /Game/BlackwoodHollow/Equipment/ArmorFit (DT_BH_BodyTypes, DT_BH_ArmorPieces).
"""
from .bodymat import apply_all as apply_body_material
from .body_types import build_body_types, cancel_build_body_types
from .fitting import fit_armor
from .items import apply_to_items
from .report import open_report
from .rigging import rig_static_pieces

__all__ = [
    "apply_body_material",
    "apply_to_items",
    "build_body_types",
    "cancel_build_body_types",
    "fit_armor",
    "open_report",
    "rig_static_pieces",
]
