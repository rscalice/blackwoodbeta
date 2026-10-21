"""13H-2: create the BH sound classes and mix. Run: Tools > Execute Python Script, or `py bh_make_13h_audio.py`."""
import unreal

FOLDER = "/Game/BlackwoodHollow/Audio/Mix"
tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary


def make(name, cls, factory):
    path = f"{FOLDER}/{name}"
    if lib.does_asset_exist(path):
        unreal.log(f"[BH_13H] exists: {path}")
        return lib.load_asset(path)
    asset = tools.create_asset(name, FOLDER, cls, factory)
    unreal.log(f"[BH_13H] created: {path}")
    return asset


master = make("SC_BH_Master", unreal.SoundClass, unreal.SoundClassFactory())
children = [make(n, unreal.SoundClass, unreal.SoundClassFactory())
            for n in ("SC_BH_Music", "SC_BH_SFX", "SC_BH_UI")]
master.set_editor_property("child_classes", children)
for c in children:
    c.set_editor_property("parent_class", master)
make("SM_BH_Master", unreal.SoundMix, unreal.SoundMixFactory())

lib.save_directory(FOLDER, only_if_is_dirty=False, recursive=False)
unreal.log("[BH_13H] done: 4 classes + 1 mix saved")
