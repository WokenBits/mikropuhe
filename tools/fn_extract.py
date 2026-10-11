#!/usr/bin/env python3
"""Reduce `rea function ... --json` envelopes to one readable text file per procedure.

    tools/fn_extract.py <dir with 0xADDR.json envelopes> evidence/functions [<dir with `rea instructions` envelopes>]

Each output file holds the facts the notes cite: body range, callers, callees,
referenced strings, the instruction list and Ghidra's pseudocode. The envelopes
themselves (about 300 KB each) are regenerable with the command in notes/NOTES.md.
"""
import glob
import json
import os
import sys

src, dst = sys.argv[1], sys.argv[2]
os.makedirs(dst, exist_ok=True)
index = []
for path in sorted(glob.glob(os.path.join(src, "0x*.json"))):
    env = json.load(open(path))
    d = env["normalized_result"]
    p = d["procedure"]
    rng = ", ".join("%s-%s" % (r["start"], r["end"]) for r in p["body"].get("ranges", []))
    lines = [
        "procedure  %s  %s" % (p["address"], p["name"]),
        "signature  %s" % p.get("signature"),
        "body       %s (%s bytes)" % (rng, p["body"].get("total_bytes")),
        "evidence   %s  provider %s %s" % (env["evidence_id"], env["provider"]["name"], env["provider"]["version"]),
        "callers    " + ", ".join("%s %s" % (c["address"], c["name"]) for c in d["callers"]),
        "callees    " + ", ".join("%s %s" % (c["address"], c["name"]) for c in d["callees"]),
        "strings    " + json.dumps(d["referenced_strings"], ensure_ascii=False),
        "",
        "-- assembly",
        *d["assembly"],
        "",
        "-- pseudocode (Ghidra decompiler; an interpretation, check against the assembly)",
        d["pseudocode"],
    ]
    open(os.path.join(dst, p["address"] + ".txt"), "w").write("\n".join(lines) + "\n")
    index.append("%s\t%s\t%s\t%d callers\t%d callees" % (
        p["address"], p["name"], p["body"].get("total_bytes"), len(d["callers"]), len(d["callees"])))
# procedures the decompiler fails on: `rea instructions` envelopes, if a third argument names their directory
if len(sys.argv) > 3:
    for path in sorted(glob.glob(os.path.join(sys.argv[3], "0x*.json"))):
        env = json.load(open(path))
        d = env["normalized_result"]
        p = d["procedure"]
        rng = ", ".join("%s-%s" % (r["start"], r["end"]) for r in p["body"].get("ranges", []))
        lines = [
            "procedure  %s  %s" % (p["address"], p["name"]),
            "body       %s (%s bytes)" % (rng, p["body"].get("total_bytes")),
            "evidence   %s  provider %s %s (rea instructions; rea function fails: decompiler error)" % (
                env["evidence_id"], env["provider"]["name"], env["provider"]["version"]),
            "",
            "-- assembly",
            *d["instructions"],
        ]
        open(os.path.join(dst, p["address"] + ".txt"), "w").write("\n".join(lines) + "\n")
        index.append("%s\t%s\t%s\tinstructions only" % (p["address"], p["name"], p["body"].get("total_bytes")))
    index.sort()
open(os.path.join(dst, "INDEX.tsv"), "w").write("\n".join(index) + "\n")
print(len(index), "procedures written to", dst)
