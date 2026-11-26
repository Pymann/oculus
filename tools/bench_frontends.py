#!/usr/bin/env python3
"""Compares the two Clang paths of Oculus: in-process shim and JSON worker.

    tools/bench_frontends.py OUT.csv NAME=DIR:BUILD_DIR [NAME=DIR:BUILD_DIR ...]

For each project and each frontend (`--frontend inproc`, `--frontend json`) it
runs `oculus -p BUILD_DIR --single-rule 40` in DIR REPEAT times (env REPEAT,
default 3) and records the wall time and the peak resident memory of the whole
process tree (oculus plus its worker processes), sampled from /proc every 10 ms.
The CSV has one row per run: project, frontend, run, units, seconds, peak_mib.
"""

import csv
import os
import subprocess
import sys
import time

OCULUS = os.path.realpath(os.environ.get("OCULUS", "out/oculus/oculus"))


def children_of(pid):
    kids = []
    task_dir = "/proc/%d/task" % pid
    try:
        for task in os.listdir(task_dir):
            with open(os.path.join(task_dir, task, "children")) as handle:
                kids.extend(int(word) for word in handle.read().split())
    except OSError:
        pass
    return kids


def rss_kib(pid):
    try:
        with open("/proc/%d/status" % pid) as handle:
            for line in handle:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1])
    except OSError:
        pass
    return 0


def tree_rss_kib(root):
    total = 0
    todo = [root]
    while todo:
        pid = todo.pop()
        total += rss_kib(pid)
        todo.extend(children_of(pid))
    return total


def run_once(directory, build_dir, frontend):
    argv = [OCULUS, "-p", build_dir, "--single-rule", "40", "--frontend", frontend]
    start = time.monotonic()
    proc = subprocess.Popen(argv, cwd=directory, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    peak = 0
    while proc.poll() is None:
        peak = max(peak, tree_rss_kib(proc.pid))
        time.sleep(0.01)
    seconds = time.monotonic() - start
    if proc.returncode not in (0, 1):
        sys.stderr.write("oculus failed (exit %d) on %s with %s\n" % (proc.returncode, build_dir, frontend))
    return seconds, peak / 1024.0


def unit_count(directory, build_dir):
    import json

    with open(os.path.join(directory, build_dir, "compile_commands.json")) as handle:
        return len(json.load(handle))


def main():
    if len(sys.argv) < 3:
        sys.stderr.write(__doc__)
        return 2
    repeat = int(os.environ.get("REPEAT", "3"))
    rows = []
    for spec in sys.argv[2:]:
        name, where = spec.split("=", 1)
        directory, build_dir = where.split(":", 1)
        units = unit_count(directory, build_dir)
        for run in range(repeat):
            for frontend in ("json", "inproc"):
                seconds, peak = run_once(directory, build_dir, frontend)
                rows.append([name, frontend, run, units, "%.3f" % seconds, "%.1f" % peak])
                print("%-8s %-7s run %d: %7.2f s  %7.1f MiB  (%d units)" % (name, frontend, run, seconds, peak, units))
    with open(sys.argv[1], "w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["project", "frontend", "run", "units", "seconds", "peak_mib"])
        writer.writerows(rows)
    return 0


if __name__ == "__main__":
    sys.exit(main())
