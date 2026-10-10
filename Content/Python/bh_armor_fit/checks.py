"""Pre-flight checks shared by every command."""
import unreal

from . import config


def preflight():
    """Raise if the editor is in a state where builds must not run; warn about unsaved work."""
    if unreal.EditorLevelLibrary.get_pie_worlds(False):
        raise RuntimeError("Play-in-editor is running; stop it first.")
    forbid_metahumans_folder()
    if not unreal.EditorAssetLibrary.does_directory_exist(config.COMMON_FOLDER):
        raise RuntimeError("Common MetaHuman folder missing: " + config.COMMON_FOLDER)
    dirty = unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
    if dirty:
        unreal.log_warning(
            "%s %d unsaved package(s) in the editor; builds may lock the editor for minutes."
            % (config.LOG_FIT, len(dirty))
        )


def forbid_metahumans_folder():
    """A MetaHuman build with a blank common folder creates /Game/MetaHumans and a duplicate skeleton."""
    if unreal.EditorAssetLibrary.does_directory_exist(config.FORBIDDEN_ROOT):
        raise RuntimeError(
            "%s exists (duplicate MetaHuman common folder). Remove it before continuing." % config.FORBIDDEN_ROOT
        )
