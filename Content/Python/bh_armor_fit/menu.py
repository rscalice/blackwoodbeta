"""The 'Blackwood Hollow > Armor Fit' main-menu entries.

Every entry reloads the bh_armor_fit package first, so edits to the tool take effect without restarting the editor.
"""
import unreal

OWNER = "BlackwoodHollow"
MAIN_MENU = "LevelEditor.MainMenu"

# Fresh import of the whole package, then run the call. Keeps menu clicks in sync with the code on disk.
_RELOAD = ("import sys; [sys.modules.pop(m) for m in list(sys.modules) if m.startswith('bh_armor_fit')]; "
           "import bh_armor_fit as f; ")

SETS = ["BaseClothing", "StarterCloth", "SpellCaster", "BasicLeather", "Platemail", "Villager"]

# (section, name, label, tooltip, call)
_COMMANDS = (
    ("Everyday", "ReharvestAll", "Re-harvest all + Apply To Items",
     "Rewrite every fitted mesh and mask from the existing builds (about 5 min, no MetaHuman build), then update the items.",
     "f.fit_armor(reuse_builds=True, force=True); f.apply_to_items()"),
    ("Everyday", "ApplyToItems", "Apply To Items", "Write the fitted meshes and masks into the armor item Blueprints.",
     "f.apply_to_items()"),
    ("Everyday", "DryRun", "Dry Run", "List what a fit would build or skip, without building.",
     "f.fit_armor(dry_run=True)"),
    ("Everyday", "OpenLastReport", "Open Last Report", "Open the most recent Saved/ArmorFit report.",
     "f.open_report()"),

    ("Full", "FitAllFresh", "Full Refit: all sets + Apply (about 1 h)",
     "MetaHuman-build every set and harvest. Start this in a FRESH editor session (nothing opened, no PIE) - "
     "regenerating outfit assets that are already loaded can crash UE 5.8.",
     "f.fit_armor(force=True); f.apply_to_items()"),
    ("Full", "BuildBodyTypes", "Build Body Types", "Create or update each body type's MetaHuman Character, auto-rig and build it.",
     "f.build_body_types()"),
    ("Full", "RigStaticPieces", "Rig Static Pieces", "Turn static helms, caps and masks into skinned skeletal meshes.",
     "f.rig_static_pieces()"),
    ("Full", "SkinShrinkMaterial", "Rebuild Skin Push-In Material",
     "Re-run bodymat_shrink: rebuild M_BH_Skin_Masked's push-in WPO (needed after a body rebuild). Close the material editor first.",
     "import bh_armor_fit.bodymat_shrink as s; s.run()"),

    ("Checks", "CheckBones", "Check: source bone pose vs body",
     "Print rest-pose bone positions/rotations of the source packs, the bodies and fitted meshes (read-only).",
     "import bh_armor_fit.inspect_bones as b; b.run(); b.run(b.BOOTS)"),
    ("Checks", "CheckWeights", "Check: fitted weights",
     "Print which bones each fitted piece is weighted to, per side (read-only).",
     "import bh_armor_fit.inspect_weights as w; w.run()"),
    ("Checks", "CheckPose", "Check: live pose (run during PIE)",
     "During PIE: print live bone positions/scales on the body and each armor piece (read-only).",
     "import bh_armor_fit.inspect_pose as p; p.run()"),
    ("Checks", "CheckNewPacks", "Check: new pack sections + authoring body",
     "Per-section stats and poke-through vs candidate bodies for new Fab packs (read-only).",
     "import bh_armor_fit.inspect_new as n; n.run()"),
)


def _entry(name, label, tooltip, call):
    entry = unreal.ToolMenuEntry(name=name, type=unreal.MultiBlockType.MENU_ENTRY)
    entry.set_label(label)
    entry.set_tool_tip(tooltip)
    entry.set_string_command(unreal.ToolMenuStringCommandType.PYTHON, "", _RELOAD + call)
    return entry


def register():
    """Add the menu to the level editor's main menu; returns False while the main menu does not exist yet."""
    menus = unreal.ToolMenus.get()
    main = menus.find_menu(MAIN_MENU)
    if main is None:
        return False
    menus.unregister_owner_by_name(OWNER)
    root = main.add_sub_menu(OWNER, "", "BlackwoodHollow", "Blackwood Hollow")
    armor_fit = root.add_sub_menu(OWNER, "", "ArmorFit", "Armor Fit")
    for section, name, label, tooltip, call in _COMMANDS:
        armor_fit.add_menu_entry(section, _entry(name, label, tooltip, call))

    # One set at a time.
    reharvest = armor_fit.add_sub_menu(OWNER, "Sets", "ReharvestSet", "Re-harvest one set + Apply")
    full = armor_fit.add_sub_menu(OWNER, "Sets", "FitSet", "Full fit one set + Apply")
    for s in SETS:
        reharvest.add_menu_entry("Sets", _entry("Reharvest_" + s, s, "Re-harvest %s from its existing build, then update items." % s,
                                                "f.fit_armor(sets=[%r], reuse_builds=True, force=True); f.apply_to_items()" % s))
        full.add_menu_entry("Sets", _entry("Fit_" + s, s, "MetaHuman-build and harvest %s (5-10 min). Fresh editor session recommended." % s,
                                           "f.fit_armor(sets=[%r], force=True); f.apply_to_items()" % s))
    menus.refresh_all_widgets()
    return True


def register_when_ready():
    """Register on the first editor tick where the main menu exists (init_unreal runs too early)."""
    state = {}

    def on_tick(_delta):
        if register():
            unreal.unregister_slate_post_tick_callback(state["handle"])

    state["handle"] = unreal.register_slate_post_tick_callback(on_tick)
