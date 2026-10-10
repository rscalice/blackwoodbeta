"""Helpers for loading, duplicating and saving assets, and for change stamps.

Saving always goes through an explicit package list; nothing here saves "all".
"""
import hashlib
import os

import unreal

_STAMP_TAG = "BH_ArmorFitStamp"


def object_path(package):
    """'/Game/A/B' -> '/Game/A/B.B'."""
    return "%s.%s" % (package, package.rsplit("/", 1)[-1])


def exists(package):
    return unreal.EditorAssetLibrary.does_asset_exist(package)


def load(package):
    asset = unreal.EditorAssetLibrary.load_asset(package)
    if asset is None:
        raise RuntimeError("Asset not found: " + package)
    return asset


def ensure_folder(folder):
    unreal.EditorAssetLibrary.make_directory(folder)


def duplicate(source, target):
    """Duplicate an asset to a new package path and return the copy."""
    ensure_folder(target.rsplit("/", 1)[0])
    copy = unreal.EditorAssetLibrary.duplicate_asset(source, target)
    if copy is None:
        raise RuntimeError("Could not duplicate %s -> %s" % (source, target))
    return copy


def list_folder(folder):
    """Package paths of the assets directly or recursively under a folder."""
    return [p.split(".")[0] for p in unreal.EditorAssetLibrary.list_assets(folder, True, False)]


def save(packages):
    """Save exactly the given packages (explicit list, never save-all)."""
    packages = list(dict.fromkeys(packages))
    if not packages:
        return True
    objects = [load(p).get_outermost() for p in packages]
    return unreal.EditorLoadingAndSavingUtils.save_packages(objects, False)


def disk_mtime(package):
    """Modification time of the saved .uasset, or 0.0 when it is not on disk."""
    relative = package[len("/Game/"):] + ".uasset"
    path = os.path.join(unreal.SystemLibrary.get_project_directory(), "Content", relative)
    return os.path.getmtime(path) if os.path.exists(path) else 0.0


def stamp_of(*parts):
    """Short fingerprint of everything an output depends on."""
    digest = hashlib.sha1("|".join(str(p) for p in parts).encode("utf-8"))
    return digest.hexdigest()[:16]


def input_stamp(*packages):
    """Fingerprint of the saved state of input assets (path + modification time)."""
    return stamp_of(*["%s@%d" % (p, int(disk_mtime(p))) for p in packages])


def read_stamp(package):
    if not exists(package):
        return ""
    return unreal.EditorAssetLibrary.get_metadata_tag(load(package), _STAMP_TAG)


def write_stamp(asset, value):
    asset.modify(True)
    unreal.EditorAssetLibrary.set_metadata_tag(asset, _STAMP_TAG, value)
