# Blackwood Hollow editor startup tweaks (runs automatically when the editor starts).
# Robert wants "Use Less CPU when in Background" OFF so PIE combat tests run at full speed.
import unreal
try:
    _perf = unreal.load_object(None, "/Script/UnrealEd.Default__EditorPerformanceSettings")
    _perf.set_editor_property("bThrottleCPUWhenNotForeground", False)
    unreal.log("[BH] Editor background CPU throttling disabled")
except Exception as e:
    unreal.log_warning("[BH] Could not disable background throttling: %s" % e)

# Blackwood Hollow > Armor Fit main-menu entries (registered once the main menu exists).
try:
    from bh_armor_fit import menu as _bh_armor_fit_menu
    _bh_armor_fit_menu.register_when_ready()
except Exception as e:
    unreal.log_warning("[BH] Could not register the Armor Fit menu: %s" % e)
