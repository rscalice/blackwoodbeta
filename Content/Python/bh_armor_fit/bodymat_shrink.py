"""Convert M_BH_Skin_Masked from HIDE (opacity mask) to SHRINK (pull skin inward under armor).

Run with the material editor for M_BH_Skin_Masked CLOSED:
    py import importlib, bh_armor_fit.bodymat_shrink as s; importlib.reload(s); s.run()

Result: Opaque material; WPO += -VertexNormalWS * max(mask_slot * shrink_slot) where shrink = BH_MaskShrinkCm (1.5)
for helm/chest/pants/boots and BH_MaskShrinkCm_Gloves (0.5) for gloves. Added on top of the original attributes'
WPO via the engine function MatLayerBlend_AddWorldPositionOffset, so nothing original is replaced.
Masks black (or bh.Armor.BodyMask 0) -> offset 0 -> body unchanged. Idempotent: re-running rebuilds the BH nodes.
"""
import unreal

MATERIAL = "/Game/BlackwoodHollow/Characters/Materials/M_BH_Skin_Masked"
ADD_WPO_FN = "/Engine/Functions/MaterialLayerFunctions/MatLayerBlend_AddWorldPositionOffset"
SLOTS = ["Helm", "Chest", "Gloves", "Pants", "Boots"]
LOG = "[BH_SkinShrink]"
MEL = unreal.MaterialEditingLibrary


def _log(msg):
    unreal.log("%s %s" % (LOG, msg))


def _expressions(mat):
    try:
        return list(mat.get_editor_property("expression_collection").get_editor_property("expressions"))
    except Exception:  # noqa: BLE001
        return list(mat.get_editor_property("expressions"))


def _new(mat, cls, x, y):
    return MEL.create_material_expression(mat, cls, x, y)


def _connect(a, a_out, b, b_in):
    if not MEL.connect_material_expressions(a, a_out, b, b_in):
        raise RuntimeError("connect failed: %s.%s -> %s.%s" % (a.get_name(), a_out, b.get_name(), b_in))


def _input_names(expr):
    try:
        return [str(n) for n in MEL.get_material_expression_input_names(expr)]
    except Exception:  # noqa: BLE001
        return []


def run():
    mat = unreal.load_asset(MATERIAL)
    if mat is None:
        raise RuntimeError("missing " + MATERIAL)
    exprs = _expressions(mat)

    # 1. the mask samplers (reuse), by parameter name
    samplers = {}
    for e in exprs:
        if isinstance(e, unreal.MaterialExpressionTextureSampleParameter2D):
            name = str(e.get_editor_property("parameter_name"))
            for slot in SLOTS:
                if name == "BH_Mask_" + slot:
                    samplers[slot] = e
    missing = [s for s in SLOTS if s not in samplers]
    if missing:
        raise RuntimeError("mask samplers missing: %s" % missing)

    # 2. the original attributes source: whatever feeds MP_MaterialAttributes, unwrapping a previous BH node
    source = MEL.get_material_property_input_node(mat, unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES)
    if source is None:
        raise RuntimeError("nothing feeds Material Attributes (is Use Material Attributes on?)")
    doomed = []
    for _ in range(3):
        is_bh = isinstance(source, unreal.MaterialExpressionSetMaterialAttributes) or (
            isinstance(source, unreal.MaterialExpressionMaterialFunctionCall)
            and source.get_editor_property("material_function")
            and "AddWorldPositionOffset" in source.get_editor_property("material_function").get_name())
        if not is_bh:
            break
        inputs = MEL.get_inputs_for_material_expression(mat, source)
        if not inputs or inputs[0] is None:
            raise RuntimeError("previous BH node %s has no attributes input wired" % source.get_name())
        doomed.append(source)  # only the BH wrapper(s) that fed Material Attributes
        source = inputs[0]
    _log("original attributes source: %s" % source.get_name())

    # 3. delete leftovers from earlier BH versions ONLY: nodes whose inputs come from the mask samplers (directly or
    #    through other BH nodes), plus our tagged / named nodes. The original skin graph's own Max/OneMinus stay.
    ours = set(samplers.values())
    changed = True
    while changed:
        changed = False
        for e in exprs:
            if e in ours or e is source:
                continue
            if not isinstance(e, (unreal.MaterialExpressionMax, unreal.MaterialExpressionOneMinus,
                                  unreal.MaterialExpressionMultiply)):
                continue
            ins = [i for i in (MEL.get_inputs_for_material_expression(mat, e) or []) if i is not None]
            if ins and any(i in ours for i in ins):
                ours.add(e)
                changed = True
    for e in exprs:
        if e in samplers.values() or e is source or e in doomed:
            continue
        if e in ours:
            doomed.append(e)
        elif isinstance(e, unreal.MaterialExpressionScalarParameter) and str(e.get_editor_property("parameter_name")).startswith("BH_MaskShrink"):
            doomed.append(e)
        elif isinstance(e, unreal.MaterialExpressionMaterialFunctionCall):
            fn = e.get_editor_property("material_function")
            if fn and "AddWorldPositionOffset" in fn.get_name():
                doomed.append(e)
        else:
            try:
                if str(e.get_editor_property("desc")) == "BH_Shrink":
                    doomed.append(e)
            except Exception:  # noqa: BLE001
                pass
    # an orphan empty SetMaterialAttributes from the crashed MCP edit (no inputs wired) is also ours
    for e in exprs:
        if isinstance(e, unreal.MaterialExpressionSetMaterialAttributes) and e not in doomed and e is not source:
            ins = [i for i in (MEL.get_inputs_for_material_expression(mat, e) or []) if i is not None]
            if not ins:
                doomed.append(e)
    for e in doomed:
        MEL.delete_material_expression(mat, e)
    _log("removed %d old BH nodes" % len(doomed))

    # 4. samplers readable in the vertex shader
    for s in samplers.values():
        s.set_editor_property("mip_value_mode", unreal.TextureMipValueMode.TMVM_MIP_LEVEL)
        s.set_editor_property("const_mip_value", 0)

    base = samplers["Helm"]
    bx, by = base.get_editor_property("material_expression_editor_x"), base.get_editor_property("material_expression_editor_y")

    def tag(e):
        try:
            e.set_editor_property("desc", "BH_Shrink")
        except Exception:  # noqa: BLE001
            pass
        return e

    shrink = _new(mat, unreal.MaterialExpressionScalarParameter, bx + 300, by - 200)
    shrink.set_editor_property("parameter_name", "BH_MaskShrinkCm")
    shrink.set_editor_property("default_value", 1.5)
    shrink_g = _new(mat, unreal.MaterialExpressionScalarParameter, bx + 300, by - 120)
    shrink_g.set_editor_property("parameter_name", "BH_MaskShrinkCm_Gloves")
    shrink_g.set_editor_property("default_value", 0.5)

    # 5. per-slot products, max chain
    products = []
    for i, slot in enumerate(SLOTS):
        m = tag(_new(mat, unreal.MaterialExpressionMultiply, bx + 500, by + i * 120))
        _connect(samplers[slot], "R", m, "A")
        _connect(shrink_g if slot == "Gloves" else shrink, "", m, "B")
        products.append(m)
    acc = products[0]
    for i, p in enumerate(products[1:]):
        mx = _new(mat, unreal.MaterialExpressionMax, bx + 700 + i * 150, by + 100)
        _connect(acc, "", mx, "A")
        _connect(p, "", mx, "B")
        acc = mx

    # 6. offset = VertexNormalWS * amount * -1
    normal = tag(_new(mat, unreal.MaterialExpressionVertexNormalWS, bx + 1100, by - 100))
    neg = tag(_new(mat, unreal.MaterialExpressionConstant, bx + 1100, by + 300))
    neg.set_editor_property("r", -1.0)
    m1 = tag(_new(mat, unreal.MaterialExpressionMultiply, bx + 1300, by))
    _connect(normal, "", m1, "A")
    _connect(acc, "", m1, "B")
    offset = tag(_new(mat, unreal.MaterialExpressionMultiply, bx + 1500, by))
    _connect(m1, "", offset, "A")
    _connect(neg, "", offset, "B")

    # 7. original attributes + offset -> Material Attributes, via the engine layer function
    fn = unreal.load_asset(ADD_WPO_FN)
    if fn is None:
        raise RuntimeError("engine function missing: " + ADD_WPO_FN)
    call = _new(mat, unreal.MaterialExpressionMaterialFunctionCall, bx + 1750, by)
    MEL.set_material_function_call(call, fn) if hasattr(MEL, "set_material_function_call") else call.set_editor_property("material_function", fn)
    names = _input_names(call)
    _log("AddWorldPositionOffset inputs: %s" % names)
    attr_in = next((n for n in names if "attrib" in n.lower() or n.lower() in ("base", "material", "in")), names[0] if names else "")
    wpo_in = next((n for n in names if n != attr_in and ("offset" in n.lower() or "wpo" in n.lower() or "position" in n.lower())),
                  next((n for n in names if n != attr_in), ""))
    _connect(source, "", call, attr_in)
    _connect(offset, "", call, wpo_in)
    if not MEL.connect_material_property(call, "", unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES):
        raise RuntimeError("could not wire the function output to Material Attributes")

    # 8. opaque, compile, save
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    MEL.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat, False)
    for mi in ("/Game/BlackwoodHollow/Characters/Materials/MI_BH_Skin_Baked_VT",
               "/Game/BlackwoodHollow/Characters/Materials/MI_BH_Body_Baked_VT_MaleNormal"):
        asset = unreal.load_asset(mi)
        if asset:
            MEL.update_material_instance(asset)
            unreal.EditorAssetLibrary.save_loaded_asset(asset, False)
    _log("done: Opaque, WPO shrink (attrs input '%s', offset input '%s'), saved" % (attr_in, wpo_in))
