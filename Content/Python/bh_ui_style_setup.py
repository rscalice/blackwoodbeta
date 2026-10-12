"""
bh_ui_style_setup.py  (Phase 13A, #46)
Run once via Tools > Execute Python Script. Idempotent: safe to re-run.

What it does (all logs are prefixed [BH_UIStyle]):
  1. Imports SourceArt/Fonts/*.ttf as FontFace assets into /Game/BlackwoodHollow/UI/Style/Fonts/
  2. Builds composite Font assets F_Cinzel (Regular, Bold) and F_Inter (Regular, Medium, SemiBold, Bold)
     with typeface entry names matching the weights.
  3. Points every CommonUI text style (TS_*) at the right Font asset (size, colour and typeface
     name were already set by the MCP build).
  4. Saves only the assets it touched.
"""
import os
import unreal

TAG = "[BH_UIStyle]"
STYLE = "/Game/BlackwoodHollow/UI/Style"
FONT_DIR = STYLE + "/Fonts"
TEXT_DIR = STYLE + "/Text"
SRC = os.path.join(unreal.Paths.project_dir(), "SourceArt", "Fonts")

FAMILIES = {
    "F_Cinzel": [("Regular", "Cinzel-Regular.ttf"), ("Bold", "Cinzel-Bold.ttf")],
    "F_Inter": [
        ("Regular", "Inter-Regular.ttf"),
        ("Medium", "Inter-Medium.ttf"),
        ("SemiBold", "Inter-SemiBold.ttf"),
        ("Bold", "Inter-Bold.ttf"),
    ],
}

# style -> font family (typeface weight name already set on the style)
STYLES = {
    "TS_Display": "F_Cinzel",
    "TS_Title": "F_Cinzel",
    "TS_Header": "F_Cinzel",
    "TS_TabInactive": "F_Cinzel",
    "TS_BodyLarge": "F_Inter",
    "TS_Body": "F_Inter",
    "TS_Caption": "F_Inter",
    "TS_Number": "F_Inter",
    "TS_Button": "F_Inter",
}

eal = unreal.EditorAssetLibrary
touched = []


def log(msg):
    unreal.log(TAG + " " + msg)


def warn(msg):
    unreal.log_warning(TAG + " " + msg)


def import_face(weight_name, ttf, family):
    face_name = "%s_%s" % (family.replace("F_", "FF_"), weight_name)
    path = "%s/%s" % (FONT_DIR, face_name)
    if eal.does_asset_exist(path):
        log("FontFace exists, skipping import: " + path)
        return path
    src = os.path.join(SRC, ttf)
    if not os.path.exists(src):
        warn("Missing source file: " + src)
        return None
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", src)
    task.set_editor_property("destination_path", FONT_DIR)
    task.set_editor_property("destination_name", face_name)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    if eal.does_asset_exist(path):
        log("Imported " + ttf + " -> " + path)
        touched.append(path)
        return path
    warn("Import failed for " + ttf)
    return None


def build_font(family, weights):
    font_path = "%s/%s" % (FONT_DIR, family)
    faces = []
    for weight, ttf in weights:
        p = import_face(weight, ttf, family)
        if p:
            faces.append((weight, p))
    if not faces:
        warn("No faces for " + family)
        return None
    if eal.does_asset_exist(font_path):
        font = eal.load_asset(font_path)
    else:
        factory = unreal.FontFactory()
        font = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            family, FONT_DIR, unreal.Font, factory)
    font.set_editor_property("font_cache_type", unreal.FontCacheType.RUNTIME)
    comp = font.get_editor_property("composite_font")
    typeface = comp.get_editor_property("default_typeface")
    entries = []
    for weight, p in faces:
        face = eal.load_asset(p)
        fd = unreal.FontData()
        fd.set_editor_property("font_face_asset", face)
        entry = unreal.TypefaceEntry()
        entry.set_editor_property("name", weight)
        entry.set_editor_property("font", fd)
        entries.append(entry)
    typeface.set_editor_property("fonts", entries)
    comp.set_editor_property("default_typeface", typeface)
    font.set_editor_property("composite_font", comp)
    eal.save_asset(font_path)
    touched.append(font_path)
    log("Built %s with %d typefaces: %s" % (family, len(entries), [w for w, _ in faces]))
    return font_path


def point_styles(fonts):
    for style, family in STYLES.items():
        font_path = fonts.get(family)
        if not font_path:
            warn("Skipping %s, %s not built" % (style, family))
            continue
        bp_path = "%s/%s" % (TEXT_DIR, style)
        cls = unreal.load_class(None, "%s.%s_C" % (bp_path, style))
        if not cls:
            warn("Cannot load class for " + bp_path)
            continue
        cdo = unreal.get_default_object(cls)
        info = cdo.get_editor_property("font")
        info.set_editor_property("font_object", eal.load_asset(font_path))
        cdo.set_editor_property("font", info)
        eal.save_asset(bp_path)
        touched.append(bp_path)
        log("%s -> %s (typeface %s)" % (style, family, info.get_editor_property("typeface_font_name")))


def main():
    log("Start. Source fonts: " + SRC)
    eal.make_directory(FONT_DIR)
    fonts = {}
    for family, weights in FAMILIES.items():
        fonts[family] = build_font(family, weights)
    point_styles(fonts)
    log("Done. Touched %d assets." % len(touched))


main()
