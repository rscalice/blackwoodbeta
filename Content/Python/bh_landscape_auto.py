"""Island 1 procedural landscape: slope / height / noise auto-layering, no paint layers.
Run in the Output Log:

    py bh_landscape_auto.py

Builds /Game/BlackwoodHollow/Environment/Landscape/M_BH_Landscape_Auto (rebuilt in place if it exists),
creates MI_BH_Landscape_Island1_Auto, assigns it to every Landscape in L_WreckageShallows and saves.
Purely procedural (world Z, vertex normal, world noise): nothing is baked, so it survives a resculpt.
Layers use STF Pack03 textures directly (MF_landscape_layer has Texture2D inputs + a MaterialAttributes
output, too awkward to drive from script).  Idempotent.  Old MI_BH_Landscape_Island1 is left alone.
"""
import unreal

TAG = "[BH_Land]"
MAT_DIR = "/Game/BlackwoodHollow/Environment/Landscape"
MAT_NAME = "M_BH_Landscape_Auto"
MI_NAME = "MI_BH_Landscape_Island1_Auto"
MAT = MAT_DIR + "/" + MAT_NAME
MI = MAT_DIR + "/" + MI_NAME
LEVEL = "/Game/Levels/WreckageShallows/L_WreckageShallows"
STF = "/Game/Fab/STF/Pack03-LandscapePro/"
TEX = STF + "Environment/Landscape/Landscape/"
TEXV2 = TEX + "v2/"
GRASS = STF + "Base/LandscapeGrassType/"
SAND = "/Game/Fab/Megascans/Surfaces/Rocky_Sand_vmkecjj/High/vmkecjj_tier_1/Textures/"

# name -> default (cm / degrees / 0-1)
DEFAULTS = {
    "SlopeStart": 35.0,      # deg; rock starts fading in
    "SlopeEnd": 45.0,        # deg; fully rock
    "BeachHeightMax": 300.0,  # cm world Z (sea level ~0)
    "BeachBlend": 150.0,     # cm soft band above BeachHeightMax
    "HighlandHeight": 3000.0,  # cm; dirt takes over above this
    "HighlandBlend": 1000.0,
    "DirtAmount": 0.25,      # fraction of grassland that becomes dirt patches
    "NoiseScale": 0.0008,    # 1/cm  (~12 m features)
    "NoiseStrength": 0.6,    # 0 = hard clean edges, 1 = very ragged
    "UVScale": 0.0033,       # 1/cm  (~3 m tile)
}

mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
MP = unreal.MaterialProperty


def log(m):
    unreal.log("%s %s" % (TAG, m))


def err(m):
    unreal.log_error("%s %s" % (TAG, m))


def load(path):
    a = eal.load_asset(path)
    if not a:
        err("missing asset " + path)
    return a


def build_material(mat):
    mel.delete_all_material_expressions(mat)
    X = [0]

    def node(cls, x, y, **props):
        e = mel.create_material_expression(mat, cls, x * 220 - 1800, y * 130)
        for k, v in props.items():
            e.set_editor_property(k, v)
        return e

    def link(a, b, pin="", apin=""):
        mel.connect_material_expressions(a, apin, b, pin)
        return b

    def sp(name, x, y):
        return node(unreal.MaterialExpressionScalarParameter, x, y,
                    parameter_name=name, default_value=DEFAULTS[name], group="BH Auto")

    def mul(a, b, x, y):
        m = node(unreal.MaterialExpressionMultiply, x, y)
        link(a, m, "A"); link(b, m, "B")
        return m

    def lerp(a, b, alpha, x, y):
        l = node(unreal.MaterialExpressionLinearInterpolate, x, y)
        link(a, l, "A"); link(b, l, "B"); link(alpha, l, "Alpha")
        return l

    def smooth(lo, hi, val, x, y):
        s = node(unreal.MaterialExpressionSmoothStep, x, y)
        link(lo, s, "Min"); link(hi, s, "Max"); link(val, s, "Value")
        return s

    def add(a, b, x, y):
        m = node(unreal.MaterialExpressionAdd, x, y)
        link(a, m, "A"); link(b, m, "B")
        return m

    def one_minus(a, x, y):
        m = node(unreal.MaterialExpressionOneMinus, x, y)
        link(a, m, "")
        return m

    def comp(a, x, y, r=False, g=False, b=False):
        m = node(unreal.MaterialExpressionComponentMask, x, y, r=r, g=g, b=b, a=False)
        link(a, m, "")
        return m

    def tex(path, uv, kind, x, y):
        t = load(path)
        e = node(unreal.MaterialExpressionTextureSample, x, y, texture=t,
                 sampler_type=kind)
        link(uv, e, "Coordinates")
        return e

    C = unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
    N = unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL
    M = unreal.MaterialSamplerType.SAMPLERTYPE_MASKS

    # ---------- world position, UVs
    wp = node(unreal.MaterialExpressionWorldPosition, 0, 0)
    wxy = comp(wp, 1, 0, r=True, g=True)
    wz = comp(wp, 1, 1, b=True)
    uvs = sp("UVScale", 1, 2)
    uv_flat = mul(wxy, uvs, 2, 0)
    # rock: vertical projection (X+Y along a diagonal, Z) so steep faces don't smear
    wx = comp(wp, 1, 3, r=True); wy = comp(wp, 1, 4, g=True)
    diag = mul(add(wx, wy, 2, 3), node(unreal.MaterialExpressionConstant, 2, 4, r=0.7071), 3, 3)
    app = node(unreal.MaterialExpressionAppendVector, 4, 3)
    link(diag, app, "A"); link(wz, app, "B")
    uv_rock = mul(app, uvs, 5, 3)

    # ---------- noise (world-aligned, 0..1)
    nscale = sp("NoiseScale", 1, 6)
    npos = mul(wp, nscale, 2, 6)
    noise = node(unreal.MaterialExpressionNoise, 3, 6, scale=1.0, levels=3,
                 output_min=0.0, output_max=1.0, turbulence=False)
    link(npos, noise, "Position")
    nstr = sp("NoiseStrength", 3, 7)

    # ---------- slope mask: rock = 1 when steeper than SlopeEnd
    nz = comp(node(unreal.MaterialExpressionVertexNormalWS, 1, 9), 2, 9, b=True)

    def cos_deg(name, y):
        d = mul(sp(name, 3, y), node(unreal.MaterialExpressionConstant, 3, y + 1, r=0.0174533), 4, y)
        c = node(unreal.MaterialExpressionCosine, 5, y, period=6.2831853)
        link(d, c, "")
        return c
    cos_s = cos_deg("SlopeStart", 10)
    cos_e = cos_deg("SlopeEnd", 12)
    rock = one_minus(smooth(cos_e, cos_s, nz, 6, 10), 7, 10)   # 0 flat -> 1 steep

    # ---------- beach mask: 1 below BeachHeightMax, soft band BeachBlend, edge wobbled by noise
    wob = mul(mul(add(noise, node(unreal.MaterialExpressionConstant, 4, 14, r=-0.5), 5, 14), nstr, 6, 14),
              sp("BeachBlend", 5, 15), 7, 14)
    zn = add(wz, wob, 8, 14)
    bmax = sp("BeachHeightMax", 8, 15)
    bend = add(bmax, sp("BeachBlend", 8, 16), 9, 15)
    beach = one_minus(smooth(bmax, bend, zn, 10, 14), 11, 14)

    # ---------- dirt mask: noise patches + optional highland
    nmix = add(mul(noise, nstr, 4, 18), mul(node(unreal.MaterialExpressionConstant, 4, 19, r=0.5),
                                            one_minus(nstr, 4, 20), 5, 19), 6, 18)
    thr = one_minus(sp("DirtAmount", 5, 21), 6, 21)
    lo = add(thr, node(unreal.MaterialExpressionConstant, 6, 22, r=-0.08), 7, 21)
    hi = add(thr, node(unreal.MaterialExpressionConstant, 6, 23, r=0.08), 7, 22)
    patch = smooth(lo, hi, nmix, 8, 20)
    hh = sp("HighlandHeight", 8, 24)
    high = smooth(hh, add(hh, sp("HighlandBlend", 8, 25), 9, 24), wz, 10, 24)
    dirt = node(unreal.MaterialExpressionMax, 11, 21)
    link(patch, dirt, "A"); link(high, dirt, "B")

    # ---------- layer samples
    g_c = tex(TEX + "T_ground_grass_01_diffuse", uv_flat, C, 12, 0)
    g_n = tex(TEX + "T_ground_grass_01_normal", uv_flat, N, 12, 1)
    L = unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR  # STF masks are flagged colour, not Masks
    g_m = tex(TEXV2 + "T_ground_grass_01_masks", uv_flat, L, 12, 2)
    d_c = tex(TEX + "T_ground_dirt_01_diffuse", uv_flat, C, 12, 4)
    d_n = tex(TEX + "T_ground_dirt_01_normal", uv_flat, N, 12, 5)
    d_m = tex(TEXV2 + "T_ground_dirt_01_masks", uv_flat, L, 12, 6)
    r_c = tex(TEX + "T_ground_rock_01_diffuse", uv_rock, C, 12, 8)
    r_n = tex(TEX + "T_ground_rock_01_normal", uv_rock, N, 12, 9)
    r_m = tex(TEXV2 + "T_ground_rock_01_masks", uv_rock, L, 12, 10)
    # beach/sand: Megascans Rocky Sand (B = base colour, N = normal, ORM: R = AO, G = roughness)
    s_c = tex(SAND + "T_vmkecjj_4K_B", uv_flat, C, 12, 12)
    s_n = tex(SAND + "T_vmkecjj_4K_N", uv_flat, N, 12, 13)
    s_m = tex(SAND + "T_vmkecjj_4K_ORM", uv_flat, M, 12, 14)
    log("beach layer = Megascans Rocky_Sand_vmkecjj")

    # ---------- blend: grass -> dirt -> beach -> rock (cliff overrides everything)
    def blend(g, d, s, r, y, out_pin=""):
        a = lerp(g, d, dirt, 14, y)
        b = lerp(a, s, beach, 15, y)
        return lerp(b, r, rock, 16, y)
    col = blend(g_c, d_c, s_c, r_c, 0)
    nrm = blend(g_n, d_n, s_n, r_n, 4)
    # STF masks: R = roughness, G = AO; Megascans ORM: R = AO, G = roughness
    rough = blend(comp(g_m, 13, 20, r=True), comp(d_m, 13, 21, r=True),
                  comp(s_m, 13, 22, g=True), comp(r_m, 13, 23, r=True), 8)
    ao = blend(comp(g_m, 13, 24, g=True), comp(d_m, 13, 25, g=True),
               comp(s_m, 13, 26, r=True), comp(r_m, 13, 27, g=True), 12)
    mel.connect_material_property(col, "", MP.MP_BASE_COLOR)
    mel.connect_material_property(nrm, "", MP.MP_NORMAL)
    mel.connect_material_property(rough, "", MP.MP_ROUGHNESS)
    mel.connect_material_property(ao, "", MP.MP_AMBIENT_OCCLUSION)

    # ---------- grass output: nothing on beach / rock
    land = mul(one_minus(beach, 17, 16), one_minus(rock, 17, 17), 18, 16)
    grass_m = mul(land, one_minus(dirt, 18, 18), 19, 17)
    dirt_m = mul(land, dirt, 19, 19)
    # Named reroutes so the masks are easy to find and wire by hand if the scripted grass output fails.
    def named(src, nm, y):
        try:
            r = node(unreal.MaterialExpressionNamedRerouteDeclaration, 20, y, name=nm)
            link(src, r, "")
            return r
        except Exception:  # noqa
            return src
    grass_r = named(grass_m, "GrassMask", 17)
    dirt_r = named(dirt_m, "DirtGrassMask", 19)
    specs = [("Grass", "grass1_layer_grass", grass_r),
             ("Flowers", "grass2_layer_flowers", grass_r),
             ("DirtGrass", "dirt_layer_grass", dirt_r)]
    go = node(unreal.MaterialExpressionLandscapeGrassOutput, 22, 17)
    try:
        gi = []
        for nm, asset, _m in specs:
            g = unreal.GrassInput()
            g.set_editor_property("name", nm)
            g.set_editor_property("grass_type", load(GRASS + asset))
            gi.append(g)
        go.set_editor_property("grass_types", gi)
        mel.recompile_material(mat)  # pins appear once the entries exist
        for nm, _a, m in specs:
            link(m, go, nm)
        log("grass output created: Grass+Flowers on GrassMask, DirtGrass on DirtGrassMask")
    except Exception as ex:  # noqa
        err("GRASS OUTPUT: wire by hand (%s). In M_BH_Landscape_Auto select the Landscape Grass Output node, "
            "add 3 Grass Types: Grass=grass1_layer_grass, Flowers=grass2_layer_flowers, "
            "DirtGrass=dirt_layer_grass (STF Base/LandscapeGrassType); connect GrassMask to Grass and Flowers, "
            "DirtGrassMask to DirtGrass; Save." % ex)

    mel.layout_material_expressions(mat) if hasattr(mel, "layout_material_expressions") else None
    mel.recompile_material(mat)


def main():
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    if eal.does_asset_exist(MAT):
        mat = eal.load_asset(MAT)
        log("rebuilding existing " + MAT)
    else:
        mat = tools.create_asset(MAT_NAME, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
        log("created " + MAT)
    if not mat:
        err("could not create material")
        return
    build_material(mat)
    eal.save_asset(MAT, only_if_is_dirty=False)

    if eal.does_asset_exist(MI):
        mi = eal.load_asset(MI)
    else:
        mi = tools.create_asset(MI_NAME, MAT_DIR, unreal.MaterialInstanceConstant,
                                unreal.MaterialInstanceConstantFactoryNew())
        log("created " + MI)
    mel.set_material_instance_parent(mi, mat)
    for k, v in DEFAULTS.items():
        mel.set_material_instance_scalar_parameter_value(mi, k, v)
    mel.update_material_instance(mi)
    eal.save_asset(MI, only_if_is_dirty=False)

    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    lands = [a for a in sub.get_all_level_actors() if isinstance(a, unreal.LandscapeProxy)]
    if not lands:
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(LEVEL)
        lands = [a for a in sub.get_all_level_actors() if isinstance(a, unreal.LandscapeProxy)]
    if not lands:
        unreal.log_warning("%s no Landscape found; open %s and rerun" % (TAG, LEVEL))
        return
    for a in lands:
        o, e = a.get_actor_bounds(False)
        log("%s world Z range %.0f .. %.0f cm (BeachHeightMax default %.0f; adjust on the MI if the waterline differs)"
            % (a.get_actor_label(), o.z - e.z, o.z + e.z, DEFAULTS["BeachHeightMax"]))
        a.set_editor_property("landscape_material", mi)
        log("landscape_material set on " + a.get_actor_label())
    unreal.EditorLoadingAndSavingUtils.save_packages([a.get_outermost() for a in lands], False)
    log("done")


main()
