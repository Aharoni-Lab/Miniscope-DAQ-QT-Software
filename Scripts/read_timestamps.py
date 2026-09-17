#!/usr/bin/env python3
"""Decode the "Unix Time Stamp (ms)" column of a recording into readable dates.

Recordings carry two clocks per row: "Time Stamp (ms)", milliseconds since
recording started, and "Unix Time Stamp (ms)", absolute UTC time in
milliseconds since the 1970 epoch. The second is what lines a session up
against a behavior rig, a TTL box, or a lab notebook - but it is a 13-digit
integer, which is unreadable by eye and easy to misread (see NOTES below).

Usage
    python Scripts/read_timestamps.py <session dir | csv file> [options]

    # a whole session (every device's timeStamps.csv, plus notes.csv)
    python Scripts/read_timestamps.py Data/MyName/Animal1/2026_09_16/22_26_35

    # one file, every row, written back out with a readable column added
    python Scripts/read_timestamps.py .../BehaviorCam/timeStamps.csv --all
    python Scripts/read_timestamps.py .../BehaviorCam/timeStamps.csv --out decoded.csv

Options
    --utc          show UTC instead of this computer's local time
    --all          print every row instead of the first and last few
    --rows N       how many rows to show at each end (default 5)
    --out FILE     write a copy of the CSV with a "Date Time" column added
    --quiet        suppress the session summary, print only rows

Stdlib only - no pandas, no numpy - so it runs against a bare Python on any
machine that holds the data.

NOTES - the three ways people misread this column:
  1. It is MILLISECONDS, not seconds. Most date functions want seconds, so
     divide by 1000 first. Read as seconds, the value lands near the year
     58,000, which at least fails obviously.
  2. The number is UTC-referenced, but the session folder name is LOCAL time.
     Decode as local (the default here) and the two agree; decode as UTC and
     they differ by your offset, which looks like a bug and is not one.
  3. The first frame or two can be slightly EARLIER than the recording start
     time, giving a negative "Time Stamp (ms)". Those frames were already in
     the ring buffer when recording began. It is expected - do not assert
     that every row is at or after recordingStartTime.
"""

import argparse
import datetime
import json
import os
import sys

UNIX_COL = "Unix Time Stamp (ms)"
REL_COL = "Time Stamp (ms)"
READABLE_COL = "Date Time"


def to_datetime(epoch_ms, utc=False):
    """Epoch milliseconds -> datetime, local by default."""
    tz = datetime.timezone.utc if utc else None
    return datetime.datetime.fromtimestamp(epoch_ms / 1000.0, tz=tz)


def format_time(epoch_ms, utc=False):
    return to_datetime(epoch_ms, utc).isoformat(sep=" ", timespec="milliseconds")


def read_csv(path):
    """Return (header, rows) with the LAST field allowed to contain commas.

    notes.csv ends in free-form note text that the software writes unquoted,
    so a naive split would shred any note containing a comma. Splitting at
    most len(header)-1 times lets the final field absorb the rest of the line.
    """
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        lines = [line.rstrip("\r\n") for line in handle if line.strip()]
    if not lines:
        return [], []
    header = lines[0].split(",")
    rows = [line.split(",", len(header) - 1) for line in lines[1:]]
    return header, rows


def find_session_meta(path):
    """Walk up from a CSV looking for the session metaData.json.

    Device CSVs sit at <session>/<Device>/timeStamps.csv and notes.csv at
    <session>/notes.csv, so the file is one or two levels up. Only the session
    metadata has recordingStartTime; a device metaData.json does not.
    """
    directory = path if os.path.isdir(path) else os.path.dirname(os.path.abspath(path))
    for _ in range(3):
        candidate = os.path.join(directory, "metaData.json")
        if os.path.isfile(candidate):
            try:
                with open(candidate, "r", encoding="utf-8") as handle:
                    meta = json.load(handle)
            except (ValueError, OSError):
                meta = {}
            if "recordingStartTime" in meta:
                return candidate, meta
        parent = os.path.dirname(directory)
        if parent == directory:
            break
        directory = parent
    return None, {}


def print_session_summary(meta, utc):
    start = meta.get("recordingStartTime", {})
    end = meta.get("recordingEndTime")

    if "msecSinceEpoch" in start:
        print("Recording started : %s" % format_time(start["msecSinceEpoch"], utc))
    if end and "msecSinceEpoch" in end:
        print("Recording ended   : %s" % format_time(end["msecSinceEpoch"], utc))
        elapsed = end.get("elapsedMonotonicMs")
        if elapsed is not None:
            print("Duration          : %.3f s" % (elapsed / 1000.0))
        drift = end.get("driftMs")
        if drift is not None:
            # The wall clock and the monotonic clock are tied together once, at
            # record start, so that an NTP step mid-recording cannot distort
            # frame intervals. The price is that they drift apart slowly. This
            # is how far apart they had moved by the end - subtract it,
            # pro-rated by row, if you need absolute accuracy across a session.
            print("Clock drift at end: %+d ms" % drift)
    elif start:
        print("Recording ended   : (no recordingEndTime - older recording, "
              "or the software did not shut down cleanly)")
    print()


def decode_file(path, args, meta):
    header, rows = read_csv(path)
    if not header:
        print("%s: empty file" % path)
        return

    label = os.path.relpath(path, args.target) if os.path.isdir(args.target) else path
    print("--- %s ---" % label)

    if not rows:
        print("(no data rows)\n")
        return

    # Prefer the per-row absolute column. Recordings made before it existed
    # still decode: rebuild absolute time from the session anchor instead, and
    # say so, because the result is only as good as that one metadata value.
    anchor = None
    if UNIX_COL in header:
        time_index = header.index(UNIX_COL)
    elif REL_COL in header:
        anchor = meta.get("recordingStartTime", {}).get("msecSinceEpoch")
        if anchor is None:
            print("No %r column and no metaData.json anchor - cannot decode.\n" % UNIX_COL)
            return
        time_index = header.index(REL_COL)
        print("No %r column (recorded before it was added); reconstructing from "
              "metaData.json recordingStartTime." % UNIX_COL)
    else:
        print("Neither %r nor %r in header: %s\n" % (UNIX_COL, REL_COL, ",".join(header)))
        return

    def epoch_of(row):
        value = int(row[time_index])
        return value + anchor if anchor is not None else value

    if args.out:
        with open(args.out, "w", encoding="utf-8", newline="") as handle:
            handle.write(",".join(header + [READABLE_COL]) + "\n")
            for row in rows:
                handle.write(",".join(row + [format_time(epoch_of(row), args.utc)]) + "\n")
        print("Wrote %s (%d rows, %r column appended)\n"
              % (args.out, len(rows), READABLE_COL))
        return

    width = max(len(",".join(row)) for row in rows)
    width = min(width, 60)

    def show(row):
        print("  %-*s  %s" % (width, ",".join(row), format_time(epoch_of(row), args.utc)))

    if args.all or len(rows) <= 2 * args.rows:
        for row in rows:
            show(row)
    else:
        for row in rows[:args.rows]:
            show(row)
        print("  ... %d more rows ..." % (len(rows) - 2 * args.rows))
        for row in rows[-args.rows:]:
            show(row)

    first, last = epoch_of(rows[0]), epoch_of(rows[-1])
    span = (last - first) / 1000.0
    print("  %d rows, span %.3f s" % (len(rows), span), end="")
    if span > 0 and len(rows) > 1:
        print(", mean interval %.1f ms (%.1f Hz)"
              % ((last - first) / (len(rows) - 1), (len(rows) - 1) / span))
    else:
        print()
    print()


def collect_targets(target):
    """A single CSV, or every timestamp-bearing CSV under a session folder."""
    if os.path.isfile(target):
        return [target]
    found = []
    notes = os.path.join(target, "notes.csv")
    for entry in sorted(os.listdir(target)):
        candidate = os.path.join(target, entry, "timeStamps.csv")
        if os.path.isfile(candidate):
            found.append(candidate)
    if os.path.isfile(notes):
        found.append(notes)
    return found


def main():
    parser = argparse.ArgumentParser(
        description="Decode Miniscope recording timestamps into readable dates.",
        epilog="Times are shown in this computer's local zone unless --utc is given.")
    parser.add_argument("target", help="a session folder, or a single .csv file")
    parser.add_argument("--utc", action="store_true",
                        help="show UTC rather than local time")
    parser.add_argument("--all", action="store_true",
                        help="print every row")
    parser.add_argument("--rows", type=int, default=5, metavar="N",
                        help="rows to show at each end (default 5)")
    parser.add_argument("--out", metavar="FILE",
                        help="write a copy of the CSV with a readable column added")
    parser.add_argument("--quiet", action="store_true",
                        help="skip the session summary")
    args = parser.parse_args()

    if not os.path.exists(args.target):
        parser.error("no such file or folder: %s" % args.target)

    targets = collect_targets(args.target)
    if not targets:
        parser.error("no timeStamps.csv or notes.csv found under %s" % args.target)
    if args.out and len(targets) > 1:
        parser.error("--out writes one file; point it at a single .csv instead")

    meta_path, meta = find_session_meta(args.target)
    if meta and not args.quiet:
        print("Session: %s" % os.path.dirname(meta_path))
        print("Times shown in %s.\n" % ("UTC" if args.utc else "local time"))
        print_session_summary(meta, args.utc)

    for path in targets:
        decode_file(path, args, meta)


if __name__ == "__main__":
    sys.exit(main())
