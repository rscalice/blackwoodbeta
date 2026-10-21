"""
bh_comment_13g_all.py  (Phase 13G-9)
Runs every Phase 13G comment-box script in order: 13g, 13g3, 13g4, 13g5, 13g6, 13g7.
Each script is defensive and idempotent (a missing asset or graph is a logged warning, a box already there is skipped),
so this is safe to run more than once. The individual scripts are kept and still work on their own.
Run: Tools > Execute Python Script > Content/Python/bh_comment_13g_all.py, then read the Output Log for lines tagged [BH_Comment].
"""
import os
import runpy

import unreal

TAG = "[BH_Comment]"
SCRIPTS = [
    "bh_comment_13g.py",
    "bh_comment_13g3.py",
    "bh_comment_13g4.py",
    "bh_comment_13g5.py",
    "bh_comment_13g6.py",
    "bh_comment_13g7.py",
]


def script_dir():
    try:
        return os.path.dirname(os.path.abspath(__file__))
    except NameError:
        return os.path.join(unreal.Paths.project_content_dir(), "Python")


def main():
    base = script_dir()
    done = 0
    for name in SCRIPTS:
        path = os.path.join(base, name)
        if not os.path.exists(path):
            unreal.log_warning("%s 13G-ALL: %s not found, skipped" % (TAG, name))
            continue
        unreal.log("%s 13G-ALL: running %s" % (TAG, name))
        try:
            runpy.run_path(path, run_name="__main__")
            done += 1
        except Exception as e:
            unreal.log_warning("%s 13G-ALL: %s failed (%s)" % (TAG, name, e))
    unreal.log("%s 13G-ALL DONE: %d of %d scripts ran" % (TAG, done, len(SCRIPTS)))


main()
