"""Paths, constants and the typed view of the two config DataTables.

DT_BH_BodyTypes and DT_BH_ArmorPieces are the single source of truth; they are
read through their JSON export so the Blueprint struct field names never leak
into the rest of the package.
"""
import json
from dataclasses import dataclass, field

import unreal

LOG_FIT = "[BH_ArmorFit]"
LOG_RIG = "[BH_ArmorRig]"

EQUIPMENT_ROOT = "/Game/BlackwoodHollow/Equipment"
ARMOR_FIT_ROOT = EQUIPMENT_ROOT + "/ArmorFit"
TEMPLATES_ROOT = ARMOR_FIT_ROOT + "/Templates"
OUTFITS_ROOT = ARMOR_FIT_ROOT + "/Outfits"
RIGGED_ROOT = EQUIPMENT_ROOT + "/Rigged"
ARMOR_ROOT = EQUIPMENT_ROOT + "/Armor"
BODY_TYPES_ROOT = "/Game/BlackwoodHollow/Characters/BodyTypes"

CANONICAL_SKELETON = (
    "/Game/MetaHumans/Common/Female/Medium/NormalWeight/Body/metahuman_base_skel"
)
# MetaHuman Creator's default common folder is the single canonical copy (2026-10-12, #108).
COMMON_FOLDER = "/Game/MetaHumans/Common"
# The retired duplicate location; a build must never recreate it.
FORBIDDEN_ROOT = "/Game/Skeletons/MetaHuman"

DT_BODY_TYPES = ARMOR_FIT_ROOT + "/DT_BH_BodyTypes"
DT_ARMOR_PIECES = ARMOR_FIT_ROOT + "/DT_BH_ArmorPieces"

TEMPLATE_DF_CA = TEMPLATES_ROOT + "/ClothAssets/DF_ca_Template"
TEMPLATE_CA = TEMPLATES_ROOT + "/ClothAssets/CA_Template"
TEMPLATE_OA = TEMPLATES_ROOT + "/OA_Template/OA_Template"
TEMPLATE_WI = TEMPLATES_ROOT + "/WI_Template"

# Body-constraint names used by the MetaHuman parametric body, per config column.
CONSTRAINT_NAMES = {"height": "Height", "chest": "Chest", "waist": "Waist", "hip": "Hip"}

RIG_NONE = "None"
RIG_BODY_WEIGHTS = "BodyWeights"
RIG_RIGID_BONE = "RigidBone"

# Fit settings of the harvest step (Armor Refit 2). Every value is part of the output stamps (weights.TRANSFER_SIGNATURE,
# clearance.SIGNATURE, mask.SIGNATURE): change one and the next fit_armor() rebuilds the affected pieces. The modules read
# them at import time, so reload the package (or restart the editor Python) after editing.
FIT = {
    # clearance (cm): wanted gap between skin and garment; larger in joint bands (elbow, knee, hip, shoulder, wrist)
    "clearance_cm": 1.0,
    "joint_clearance_cm": 1.5,
    "joint_gain": 2.0,            # joint factor = min(1, gain * min(weight on bone A, weight on bone B) of the joint)
    # near-skin weight transfer from the body (all bones, incl. twist / corrective), blended into the cleaned weights
    "transfer": True,
    # which bones the transfer may use: "core_twist" folds every helper / corrective bone (side, bulge, mcp, pip, palm, Cor, kneeBack ...)
    # into its nearest core ancestor (twist_01/02 bones are core); "all" keeps the body's weights on every bone (old behaviour)
    "transfer_keep": "core_twist",
    "transfer_full_cm": 4.0,      # within this distance of the skin the body's weights are used as they are
    "transfer_fade_cm": 4.0,      # ... fading out to the cleaned weights over this much further distance
    "transfer_smooth_rings": 2,   # smoothing passes over the blended zone
    # body mask bake
    "mask_size": 512,
    "mask_reach_cm": 5.0,         # body vertex is covered when the garment is within this distance along its normal
    "mask_align": 0.5,            # cosine between the skin normal and the direction to the garment
    "mask_edge_margin_cm": 3.0,   # ... and the garment point is at least this far inside its boundary loops
    "mask_erode_rings": 1,
    # posed check (report only): off by default, fit_armor(posed_check=True) turns it on
    "posed_check": False,
}


def _package(ref):
    """Package path of a JSON-exported soft reference, or '' when it is empty."""
    name = ref["AssetPath"]["PackageName"]
    return "" if name in ("", "None") else name


@dataclass
class BodyType:
    """One row of DT_BH_BodyTypes."""

    name: str
    sex: str
    height: float
    chest: float
    waist: float
    hip: float
    base_character: str
    extra_constraints: dict = field(default_factory=dict)

    @property
    def root(self):
        return "%s/%s" % (BODY_TYPES_ROOT, self.name)

    @property
    def mhc_path(self):
        return "%s/MHC_%s" % (self.root, self.name)

    @property
    def built_root(self):
        return self.root + "/Built"

    @property
    def fit_root(self):
        return self.root + "/Built_Fit"

    @property
    def body_mesh_path(self):
        return "%s/MHC_%s/Body/SKM_MHC_%s_BodyMesh" % (self.built_root, self.name, self.name)

    @property
    def face_mesh_path(self):
        return "%s/MHC_%s/Face/SKM_MHC_%s_FaceMesh" % (self.built_root, self.name, self.name)

    def constraint_targets(self):
        """Constraint name -> target measurement for every constraint this row sets."""
        targets = {CONSTRAINT_NAMES[k]: getattr(self, k) for k in CONSTRAINT_NAMES}
        targets.update(self.extra_constraints)
        return targets


@dataclass
class Piece:
    """One row of DT_BH_ArmorPieces plus the paths derived from it."""

    name: str
    set_name: str
    slot: str
    source_mesh: str
    source_body: str
    rig_mode: str
    rigid_bone: str
    skip_resize: bool
    sections_to_keep: list
    material_overrides: dict
    target_item: str
    locked: bool
    enabled: bool
    rigged_path: str = ""
    fit_id: str = ""

    @property
    def fit_source(self):
        """The mesh that goes into the resize step: the rigged copy when the row is rigged."""
        return self.rigged_path or self.source_mesh

    @property
    def outfit_dir(self):
        return "%s/%s/%s" % (OUTFITS_ROOT, self.set_name, self.fit_id.split("_", 1)[1])

    def final_path(self, body):
        """Final mesh for a body type; rigid, non-resized pieces use the rigged mesh itself."""
        if self.skip_resize:
            return self.rigged_path or self.source_mesh
        return "%s/%s/SKM_%s_%s_%s" % (ARMOR_ROOT, self.set_name, self.set_name, self.slot, body.name)

    def mask_path(self, body):
        """Body mask texture written next to the final mesh; '' for pieces that get none (rigid helms, never resized)."""
        if self.skip_resize or self.slot == "Helm":
            return ""
        return "%s/%s/T_BHMask_%s_%s_%s" % (ARMOR_ROOT, self.set_name, self.set_name, self.slot, body.name)


def _read_table(path):
    table = unreal.load_asset(path)
    if table is None:
        raise RuntimeError("Config table missing: " + path)
    return json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))


def load_body_types():
    """Body types in table order."""
    return [
        BodyType(
            name=row["Name"],
            sex=row["Sex"],
            height=row["Height"],
            chest=row["Chest"],
            waist=row["Waist"],
            hip=row["Hip"],
            base_character=_package(row["BaseCharacter"]),
            extra_constraints={c["Name"]: c["Value"] for c in row["ExtraConstraints"]},
        )
        for row in _read_table(DT_BODY_TYPES)
    ]


def load_pieces():
    """Enabled armor pieces in table order, with rigged paths and fit ids resolved."""
    pieces = [
        Piece(
            name=row["Name"],
            set_name=row["Set"],
            slot=row["Slot"],
            source_mesh=_package(row["SourceMesh"]),
            source_body=_package(row["SourceBody"]),
            rig_mode=row["RigMode"],
            rigid_bone=row["RigidBoneName"],
            skip_resize=row["SkipResize"],
            sections_to_keep=list(row["SectionsToKeep"]),
            material_overrides={o["SlotName"]: _package(o["Material"]) for o in row["MaterialOverrides"]},
            target_item=_package(row["TargetItem"]),
            locked=row["Locked"],
            enabled=row["Enabled"],
        )
        for row in _read_table(DT_ARMOR_PIECES)
    ]
    pieces = [p for p in pieces if p.enabled]
    _resolve_rigged_paths(pieces)
    _resolve_fit_ids(pieces)
    return pieces


def _resolve_rigged_paths(pieces):
    """Rows that rig the same mesh the same way share one rigged asset, owned by the first row."""
    owners = {}
    for piece in pieces:
        if piece.rig_mode == RIG_NONE:
            continue
        key = (piece.source_mesh, piece.rig_mode, piece.rigid_bone)
        owner = owners.setdefault(key, piece)
        piece.rigged_path = "%s/%s/SK_%s_%s" % (RIGGED_ROOT, owner.set_name, owner.set_name, owner.slot)


def _resolve_fit_ids(pieces):
    """Pieces of one set that share a fit source share one outfit ('<Set>_Combined')."""
    for piece in pieces:
        group = [q for q in pieces if q.set_name == piece.set_name and q.fit_source == piece.fit_source]
        suffix = "Combined" if len(group) > 1 else piece.slot
        piece.fit_id = "%s_%s" % (piece.set_name, suffix)


def pieces_of_set(pieces, set_name):
    return [p for p in pieces if p.set_name == set_name]


def set_names(pieces):
    """Distinct set names in table order."""
    return list(dict.fromkeys(p.set_name for p in pieces))


def fit_groups(set_pieces):
    """Resize groups of one set: fit_id -> pieces sharing that outfit (skip-resize pieces excluded)."""
    groups = {}
    for piece in set_pieces:
        if not piece.skip_resize:
            groups.setdefault(piece.fit_id, []).append(piece)
    return groups
