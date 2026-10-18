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
    "SandUVScale": 0.0025,   # 1/cm  (~4 m tile)
    "SandBrightness": 0.7,   # base-colour multiplier for the sand
    "MacroScale": 0.00005,   # 1/cm  (~200 m light/dark variation, hides tiling)
    "MacroStrength": 0.3,    # 0 = off, 0.3 = +/-30% brightness
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


def expr_count(mat):
    try:
        return mel.get_num_material_expressions(mat)
    except Exception:  # noqa
        return -1


def clean_material(mat):
    """delete_all_material_expressions has been seen to leave the previous build's nodes behind (the saved
    material held 22 TextureSamples instead of 12), which desyncs the referenced-texture list from the
    compiled one -> 'Requesting an invalid TextureIndex! (11 / 10)' and a material that never finishes
    compiling.  Verify the graph is really empty; otherwise delete and recreate the asset (landscapes are
    parked on WorldGridMaterial by now, so nothing references it)."""
    for _ in range(3):
        mel.delete_all_material_expressions(mat)
        if expr_count(mat) == 0:
            return mat
    left = expr_count(mat)
    log("delete_all left %d stale expressions; recreating %s" % (left, MAT))
    mi_asset = load(MI) if eal.does_asset_exist(MI) else None
    if mi_asset:
        try:
            mel.set_material_instance_parent(mi_asset, None)
        except Exception:  # noqa
            pass
    if not eal.delete_asset(MAT):
        err("could not delete stale %s; close its editor tab and rerun" % MAT)
        return mat
    new = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        MAT_NAME, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    log("recreated material, %d expressions" % expr_count(new))
    return new or mat


def build_material(mat):
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
    uv_sand = mul(wxy, sp("SandUVScale", 10, 12), 11, 12)
    s_raw = tex(SAND + "T_vmkecjj_4K_B", uv_sand, C, 12, 12)
    s_c = mul(s_raw, sp("SandBrightness", 12, 15), 13, 13)
    s_n = tex(SAND + "T_vmkecjj_4K_N", uv_sand, N, 12, 13)
    s_m = tex(SAND + "T_vmkecjj_4K_ORM", uv_sand, M, 12, 14)
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
    # macro variation: broad light/dark noise so texture repeats don't line up
    mpos = mul(wp, sp("MacroScale", 14, 30), 15, 30)
    mnoise = node(unreal.MaterialExpressionNoise, 16, 30, scale=1.0, levels=2,
                  output_min=0.0, output_max=1.0, turbulence=False)
    link(mpos, mnoise, "Position")
    mstr2 = mul(sp("MacroStrength", 16, 31), node(unreal.MaterialExpressionConstant, 16, 32, r=2.0), 17, 31)
    centred = add(mnoise, node(unreal.MaterialExpressionConstant, 17, 30, r=-0.5), 18, 30)
    macro = add(mul(centred, mstr2, 19, 30), node(unreal.MaterialExpressionConstant, 19, 31, r=1.0), 20, 30)
    col = mul(col, macro, 21, 0)
    mel.connect_material_property(col, "", MP.MP_BASE_COLOR)
    mel.connect_material_property(nrm, "", MP.MP_NORMAL)
    mel.connect_material_property(rough, "", MP.MP_ROUGHNESS)
    mel.connect_material_property(ao, "", MP.MP_AMBIENT_OCCLUSION)

    # ---------- grass output: nothing on beach / rock
    land = mul(one_minus(beach, 17, 16), one_minus(rock, 17, 17), 18, 16)
    # Mirrors STF v2 (M_landscape_pro_v2_no_tesselation): every grass type has its OWN input, each
    # = (layer weight) x LandscapeVisibilityMask (the grass cutout for landscape holes).  Our
    # procedural masks stand in for the paint-layer weights.  Grass must NOT be gated by the
    # highland term: above HighlandHeight the surface is 100% "dirt", so a (1-dirt) grass mask is 0 there.
    grass_m = mul(land, one_minus(dirt, 18, 18), 19, 17)             # grass / flowers
    dirt_m = mul(mul(land, dirt, 19, 19), one_minus(high, 19, 20), 20, 19)   # weeds on dirt patches, lowland
    forest_m = mul(land, high, 19, 21)                                # forest + nettles on high ground
    specs = [("Grass", "grass1_layer_grass", grass_m),
             ("GrassDirt", "grass1_layer_dirt_V2", grass_m),
             ("Flowers", "grass2_layer_flowers", grass_m),
             ("DirtGrass", "dirt_layer_grass", dirt_m),
             ("ForestMain", "forest_layer_main_v2", forest_m),
             ("ForestNettles", "forest_layer_nessels_v2", forest_m)]
    go = node(unreal.MaterialExpressionLandscapeGrassOutput, 23, 18)
    try:
        gi = []
        feeds = []
        for i, (nm, asset, m) in enumerate(specs):
            g = unreal.GrassInput()
            g.set_editor_property("name", nm)
            g.set_editor_property("grass_type", load(GRASS + asset))
            gi.append(g)
            # separate Multiply per pin: mask x visibility mask (own node each, like STF's StaticSwitch inputs)
            # No LandscapeVisibilityMask: Island 1 has no visibility (hole) layer, and the mask appears to
            # zero every entry in the grass pass (nothing spawned with it; dirt rocks spawned without it).
            feeds.append(mul(m, node(unreal.MaterialExpressionConstant, 20, 23 + i * 2, r=1.0), 21, 22 + i * 2))
        go.set_editor_property("grass_types", gi)
        # GrassInput.Input is protected in Python, so connect through the named pins. The pins only
        # exist after a compile (cheap: the landscape is parked on WorldGridMaterial meanwhile).
        mel.recompile_material(mat)
        linked = 0
        for (nm, _a, _m), f in zip(specs, feeds):
            if mel.connect_material_expressions(f, "", go, nm):
                linked += 1
            else:
                err("grass pin %s not linked" % nm)
        log("grass output created: %d entries, %d linked, each with its own mask x visibility multiply"
            % (len(specs), linked))
    except Exception as ex:  # noqa
        err("GRASS OUTPUT: wire by hand (%s). In M_BH_Landscape_Auto select the Landscape Grass Output node, "
            "add the Grass Types from STF Pack03 Base/LandscapeGrassType (grass1_layer_grass, "
            "grass1_layer_dirt_V2, grass2_layer_flowers, dirt_layer_grass, forest_layer_main_v2, "
            "forest_layer_nessels_v2) and feed each from its own mask x Landscape Visibility Mask; Save." % ex)

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
    # GPU safety: park every landscape on a cheap engine material while the heavy graph is rebuilt
    # (a live recompile of this material on the whole landscape crashed the GPU).  The MI is assigned at the end.
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    cheap = eal.load_asset("/Engine/EngineMaterials/WorldGridMaterial")
    pre = [a for a in sub.get_all_level_actors() if isinstance(a, unreal.LandscapeProxy)]
    if cheap:
        for a in pre:
            a.set_editor_property("landscape_material", cheap)
    mat = clean_material(mat)
    build_material(mat)
    log("material graph has %d expressions after build" % expr_count(mat))
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
    unreal.SystemLibrary.execute_console_command(None, "grass.FlushCache")
    log("done (grass cache flushed)")


main()
