"""Logging, progress dialog and the JSON run report."""
import datetime
import json
import os

import unreal

from . import config


def log(prefix, message):
    unreal.log("%s %s" % (prefix, message))


def warn(prefix, message):
    unreal.log_warning("%s %s" % (prefix, message))


def error(prefix, message):
    unreal.log_error("%s %s" % (prefix, message))


def _report_dir():
    return os.path.join(unreal.SystemLibrary.get_project_directory(), "Saved", "ArmorFit")


class Progress:
    """Cancellable progress bar around a synchronous loop (use as a context manager)."""

    def __init__(self, total, title):
        self._task = unreal.ScopedSlowTask(max(total, 1), title)
        self._task.make_dialog(True)

    def __enter__(self):
        self._task.__enter__()
        return self

    def __exit__(self, *exc):
        return self._task.__exit__(*exc)

    def step(self, message):
        self._task.enter_progress_frame(1, message)

    @property
    def cancelled(self):
        return self._task.should_cancel()


class Report:
    """Collects one entry per unit of work and writes them to Saved/ArmorFit/report_<timestamp>.json."""

    def __init__(self, command, dry_run=False):
        self.command = command
        self.dry_run = dry_run
        self.entries = []
        self.started = datetime.datetime.now()

    def add(self, item, status, detail="", seconds=None):
        """status is one of PLAN, OK, SKIP, WARN, FAIL."""
        self.entries.append({"item": item, "status": status, "detail": detail, "seconds": seconds})

    def counts(self):
        counts = {}
        for entry in self.entries:
            counts[entry["status"]] = counts.get(entry["status"], 0) + 1
        return counts

    def finish(self, prefix):
        folder = _report_dir()
        os.makedirs(folder, exist_ok=True)
        path = os.path.join(folder, "report_%s.json" % self.started.strftime("%Y%m%d_%H%M%S_%f")[:-3])
        document = {
            "command": self.command,
            "dry_run": self.dry_run,
            "started": self.started.isoformat(timespec="seconds"),
            "counts": self.counts(),
            "entries": self.entries,
        }
        with open(path, "w", encoding="utf-8") as handle:
            json.dump(document, handle, indent=1)
        log(prefix, "%s done %s -> %s" % (self.command, self.counts(), path))
        return path


def open_report():
    """Open the most recent report in the default viewer."""
    folder = _report_dir()
    reports = sorted(f for f in os.listdir(folder) if f.startswith("report_")) if os.path.isdir(folder) else []
    if not reports:
        warn(config.LOG_FIT, "No report found in " + folder)
        return None
    path = os.path.join(folder, reports[-1])
    os.startfile(path)
    return path
