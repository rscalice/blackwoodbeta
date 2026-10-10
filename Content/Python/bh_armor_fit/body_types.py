"""Build Body Types: create or update one MetaHuman Character per config row, auto-rig it and build it.

The auto-rig is asynchronous, so the run is a small state machine driven by a Slate post-tick
callback; it never sleeps or blocks while waiting. The build itself is synchronous (it locks the
editor for several minutes) and runs inside one tick.

Re-entrancy: the MetaHuman build pumps Slate while it runs, which fires the post-tick callback
again from inside the build. `_tick` therefore ignores calls while a stage is still executing;
without that guard every pumped frame started another nested build until the editor ran out of
window handles and crashed (2026-10-09).
"""
import time

import unreal

from . import assets, checks, config, metahuman
from . import report as rpt

RIG_TIMEOUT_SECONDS = 600

_job = None


def _expected_stamp(body):
    return assets.stamp_of(
        sorted(body.constraint_targets().items()),
        assets.input_stamp(body.base_character),
    )


def _is_up_to_date(body):
    return assets.read_stamp(body.body_mesh_path) == _expected_stamp(body)


def build_body_types(names=None, dry_run=False):
    """Create or update the MetaHuman Character of each body type (all rows, or those in `names`).

    A dry run only reports what would happen. A real run returns immediately and finishes on
    editor ticks; progress goes to the Output Log (prefix [BH_ArmorFit]) and the report.
    """
    global _job
    if _job is not None:
        raise RuntimeError("A body-type build is already running; call cancel_build_body_types() to stop it.")
    bodies = [b for b in config.load_body_types() if names is None or b.name in names]
    if not bodies:
        rpt.warn(config.LOG_FIT, "No matching body types.")
        return None
    checks.preflight()
    run = rpt.Report("build_body_types", dry_run)
    if dry_run:
        for number, body in enumerate(bodies, 1):
            if _is_up_to_date(body):
                run.add(body.name, "SKIP", "up to date")
                rpt.log(config.LOG_FIT, "%d/%d %s ... up to date, skipped" % (number, len(bodies), body.name))
            else:
                action = "update" if assets.exists(body.mhc_path) else "create from " + body.base_character
                run.add(body.name, "PLAN", "would %s, auto-rig if needed, then build" % action)
                rpt.log(config.LOG_FIT, "%d/%d %s ... would %s and build" % (number, len(bodies), body.name, action))
        return run.finish(config.LOG_FIT)
    _job = _BuildJob(bodies, run)
    _job.start()
    return None


def cancel_build_body_types():
    """Stop a running body-type build after its current step."""
    if _job is not None:
        _job.stop("cancelled by user")


class _BuildJob:
    """One pass over the body types, advanced one stage per editor tick."""

    def __init__(self, bodies, run):
        self._bodies = bodies
        self._run = run
        self._index = -1
        self._body = None
        self._mhc = None
        self._stage = "next"
        self._stage_started = 0.0
        self._started = 0.0
        self._handle = None
        self._busy = False

    def start(self):
        self._handle = unreal.register_slate_post_tick_callback(self._tick)

    def stop(self, reason):
        if self._body is not None:
            self._run.add(self._body.name, "FAIL", reason)
            rpt.error(config.LOG_FIT, "%s ... %s" % (self._body.name, reason))
            self._close_edit()
        self._finish()

    def _finish(self):
        global _job
        unreal.unregister_slate_post_tick_callback(self._handle)
        _job = None
        self._run.finish(config.LOG_FIT)

    def _close_edit(self):
        if self._mhc is not None:
            metahuman.close_edit(self._mhc)
            self._mhc = None

    def _tick(self, _delta):
        if self._busy:  # re-entered from Slate pumping inside a running stage (e.g. the build)
            return
        self._busy = True
        try:
            getattr(self, "_stage_" + self._stage)()
        except Exception as exc:  # report and move on to the next body
            self._fail(str(exc))
        finally:
            self._busy = False

    def _fail(self, reason):
        self._run.add(self._body.name, "FAIL", reason)
        rpt.error(config.LOG_FIT, "%s ... %s" % (self._body.name, reason))
        self._close_edit()
        self._stage = "next"

    def _label(self):
        return "%d/%d %s" % (self._index + 1, len(self._bodies), self._body.name)

    def _stage_next(self):
        self._index += 1
        if self._index >= len(self._bodies):
            self._body = None
            self._finish()
            return
        self._body = self._bodies[self._index]
        self._started = time.time()
        self._stage = "prepare"

    def _stage_prepare(self):
        body = self._body
        if _is_up_to_date(body):
            self._run.add(body.name, "SKIP", "up to date")
            rpt.log(config.LOG_FIT, "%s ... up to date, skipped" % self._label())
            self._stage = "next"
            return
        if not assets.exists(body.mhc_path):
            assets.duplicate(body.base_character, body.mhc_path)
            rpt.log(config.LOG_FIT, "%s ... created %s from %s" % (self._label(), body.mhc_path, body.base_character))
        self._mhc = assets.load(body.mhc_path)
        rpt.log(config.LOG_FIT, "%s ... opening for edit (can take a minute)" % self._label())
        metahuman.open_for_edit(self._mhc)
        changed = metahuman.apply_constraints(self._mhc, body.constraint_targets())
        if changed or not metahuman.can_build(self._mhc):
            metahuman.start_auto_rig(self._mhc)
            self._stage_started = time.time()
            rpt.log(config.LOG_FIT, "%s ... auto-rig started" % self._label())
            self._stage = "rigging"
        else:
            self._stage = "build"

    def _stage_rigging(self):
        if metahuman.can_build(self._mhc):
            rpt.log(config.LOG_FIT, "%s ... auto-rig done (%d s)" % (self._label(), time.time() - self._stage_started))
            self._stage = "build"
        elif time.time() - self._stage_started > RIG_TIMEOUT_SECONDS:
            self._fail("auto-rig did not finish within %d s" % RIG_TIMEOUT_SECONDS)

    def _stage_build(self):
        body = self._body
        rpt.log(config.LOG_FIT, "%s ... building (locks the editor for a few minutes)" % self._label())
        build_started = time.time()
        metahuman.clear_outfits(self._mhc)
        metahuman.build(self._mhc, body.built_root)
        assets.write_stamp(assets.load(body.body_mesh_path), _expected_stamp(body))
        assets.save([body.mhc_path] + assets.list_folder(body.built_root))
        self._close_edit()
        seconds = round(time.time() - self._started)
        self._run.add(body.name, "OK", "built to " + body.built_root, seconds)
        rpt.log(config.LOG_FIT, "%s ... build OK (%d s, build step %d s)" % (self._label(), seconds, time.time() - build_started))
        self._stage = "next"
