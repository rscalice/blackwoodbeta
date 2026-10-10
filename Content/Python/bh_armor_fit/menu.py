"""The 'Blackwood Hollow > Armor Fit' main-menu entries."""
import unreal

OWNER = "BlackwoodHollow"
MAIN_MENU = "LevelEditor.MainMenu"
SECTION = "ArmorFitCommands"

_IMPORT = "import bh_armor_fit as f; "
_COMMANDS = (
    ("BuildBodyTypes", "Build Body Types", "Create or update each body type's MetaHuman Character, auto-rig and build it.", "f.build_body_types()"),
    ("RigStaticPieces", "Rig Static Pieces", "Turn static helms, caps and masks into skinned skeletal meshes.", "f.rig_static_pieces()"),
    ("FitArmorAll", "Fit Armor (all)", "Build every armor set on every body type and harvest the fitted meshes.", "f.fit_armor()"),
    ("FitArmorReharvest", "Fit Armor (re-harvest, no build)", "Rewrite the fitted meshes from the builds already in Built_Fit, without running MetaHuman builds.", "f.fit_armor(reuse_builds=True)"),
    ("FitArmorDryRun", "Fit Armor (dry run)", "List what Fit Armor would build or skip, without building.", "f.fit_armor(dry_run=True)"),
    ("ApplyToItems", "Apply To Items", "Write the fitted meshes into the armor item Blueprints.", "f.apply_to_items()"),
    ("OpenLastReport", "Open Last Report", "Open the most recent Saved/ArmorFit report.", "f.open_report()"),
)


def register():
    """Add the menu to the level editor's main menu; returns False while the main menu does not exist yet."""
    menus = unreal.ToolMenus.get()
    main = menus.find_menu(MAIN_MENU)
    if main is None:
        return False
    menus.unregister_owner_by_name(OWNER)
    root = main.add_sub_menu(OWNER, "", "BlackwoodHollow", "Blackwood Hollow")
    armor_fit = root.add_sub_menu(OWNER, "", "ArmorFit", "Armor Fit")
    for name, label, tooltip, call in _COMMANDS:
        entry = unreal.ToolMenuEntry(name=name, type=unreal.MultiBlockType.MENU_ENTRY)
        entry.set_label(label)
        entry.set_tool_tip(tooltip)
        entry.set_string_command(unreal.ToolMenuStringCommandType.PYTHON, "", _IMPORT + call)
        armor_fit.add_menu_entry(SECTION, entry)
    menus.refresh_all_widgets()
    return True


def register_when_ready():
    """Register on the first editor tick where the main menu exists (init_unreal runs too early)."""
    state = {}

    def on_tick(_delta):
        if register():
            unreal.unregister_slate_post_tick_callback(state["handle"])

    state["handle"] = unreal.register_slate_post_tick_callback(on_tick)
