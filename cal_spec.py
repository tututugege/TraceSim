#!/usr/bin/env python3
import glob
import os
import re
from datetime import datetime

# ================= User Config =================
LOG_ROOT_DIR = "./results_ckpt"
WEIGHTS_DIR = "/share/personal/S/houruyao/simpoint/rv32imab_bbv_1gb_ram"
DEBUG = True
LOG_STATUS_REPORT = os.path.join(LOG_ROOT_DIR, "log_status_report.txt")
PERF_REPORT = os.path.join(LOG_ROOT_DIR, "perf_report.txt")
# ==============================================

REGEX_ANSI = re.compile(r"\x1b\[[0-9;]*m")
REGEX_SP_ID = re.compile(r"ckpt_sp(\d+)_")
REGEX_IPC = re.compile(r"Overall IPC:\s+([0-9]+(?:\.[0-9]+)?)")
REGEX_CACHE_LINE = re.compile(
    r"^(I-Cache|D-Cache|LLC\(shared\)):\s+(\d+)\s+accesses,\s+Hit Rate:\s+([0-9]+(?:\.[0-9]+)?)%",
    re.MULTILINE,
)


def dbg(msg):
    if DEBUG:
        print(msg)


def strip_ansi(s):
    return REGEX_ANSI.sub("", s)


def bench_aliases(bench_name):
    aliases = [bench_name]
    for suffix in ("_ref", "_base", "_peak", "_test"):
        if bench_name.endswith(suffix):
            aliases.append(bench_name[: -len(suffix)])
    if "_" in bench_name:
        aliases.append(bench_name.split("_", 1)[0])
    out = []
    seen = set()
    for a in aliases:
        if a and a not in seen:
            out.append(a)
            seen.add(a)
    return out


def load_weights(bench_name):
    for a in bench_aliases(bench_name):
        for p in (
            os.path.join(WEIGHTS_DIR, f"{a}.weights"),
            os.path.join(WEIGHTS_DIR, f"{a}.pp.weights"),
        ):
            if os.path.exists(p):
                w = {}
                with open(p, "r", encoding="utf-8", errors="ignore") as f:
                    for idx, line in enumerate(f):
                        parts = line.strip().split()
                        if not parts:
                            continue
                        wt = float(parts[0])
                        sp_id = int(parts[1]) if len(parts) > 1 else idx
                        w[sp_id] = wt
                dbg(f"[INFO] weights loaded: {p}, entries={len(w)}")
                return w
    return None


def parse_log(filepath):
    try:
        with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
            content = strip_ansi(f.read())
    except Exception as e:
        return None, f"open/read failed: {e}"

    if "--- Phase Simulation Statistics ---" not in content:
        return None, "missing phase statistics section"

    ipc_matches = REGEX_IPC.findall(content)
    if not ipc_matches:
        return None, "missing Overall IPC"
    ipc = float(ipc_matches[-1])  # use last phase

    cache = {
        "l1i_access": 0.0,
        "l1i_hit": 0.0,
        "l1d_access": 0.0,
        "l1d_hit": 0.0,
        "llc_access": 0.0,
        "llc_hit": 0.0,
    }

    for name, acc_s, hit_pct_s in REGEX_CACHE_LINE.findall(content):
        acc = float(acc_s)
        hit = acc * (float(hit_pct_s) / 100.0)
        if name == "I-Cache":
            cache["l1i_access"] = acc
            cache["l1i_hit"] = hit
        elif name == "D-Cache":
            cache["l1d_access"] = acc
            cache["l1d_hit"] = hit
        elif name == "LLC(shared)":
            cache["llc_access"] = acc
            cache["llc_hit"] = hit

    return {"ipc": ipc, "cache": cache}, None


def hit_rate(hit, acc):
    return (hit / acc) * 100.0 if acc > 0 else 0.0


def process_benchmark(bench_path):
    status = []
    perf = []

    bench_name = os.path.basename(bench_path)
    weights = load_weights(bench_name)
    if not weights:
        status.append(f"[SKIP] {bench_name}: no usable weight file")
        return status, perf

    logs = sorted(glob.glob(os.path.join(bench_path, "*.log")))
    if not logs:
        status.append(f"[SKIP] {bench_name}: no .log files")
        return status, perf

    used = 0
    skipped = 0
    reasons = {}

    w_sum = 0.0
    w_ipc_sum = 0.0
    w_l1i_acc = w_l1i_hit = 0.0
    w_l1d_acc = w_l1d_hit = 0.0
    w_llc_acc = w_llc_hit = 0.0

    for logf in logs:
        name = os.path.basename(logf)
        m = REGEX_SP_ID.search(name)
        if not m:
            skipped += 1
            reasons["missing ckpt_sp id in filename"] = reasons.get(
                "missing ckpt_sp id in filename", 0
            ) + 1
            continue
        sp_id = int(m.group(1))
        if sp_id not in weights:
            skipped += 1
            reasons["sp id not found in weights"] = reasons.get(
                "sp id not found in weights", 0
            ) + 1
            continue

        parsed, reason = parse_log(logf)
        if not parsed:
            skipped += 1
            reasons[reason] = reasons.get(reason, 0) + 1
            continue

        w = weights[sp_id]
        c = parsed["cache"]
        used += 1
        w_sum += w
        w_ipc_sum += w * parsed["ipc"]
        w_l1i_acc += w * c["l1i_access"]
        w_l1i_hit += w * c["l1i_hit"]
        w_l1d_acc += w * c["l1d_access"]
        w_l1d_hit += w * c["l1d_hit"]
        w_llc_acc += w * c["llc_access"]
        w_llc_hit += w * c["llc_hit"]

    status.append(f"Processing: {bench_name}")
    status.append(f"  logs total: {len(logs)}")
    status.append(f"  logs used:  {used}")
    status.append(f"  logs skip:  {skipped}")
    for k, v in sorted(reasons.items(), key=lambda x: -x[1]):
        status.append(f"    - {k}: {v}")

    if used == 0 or w_sum <= 0.0:
        perf.append("-" * 60)
        perf.append(f"Benchmark: {bench_name}")
        perf.append("No valid logs.")
        perf.append("=" * 60)
        return status, perf

    ipc = w_ipc_sum / w_sum
    l1i_hr = hit_rate(w_l1i_hit, w_l1i_acc)
    l1d_hr = hit_rate(w_l1d_hit, w_l1d_acc)
    llc_hr = hit_rate(w_llc_hit, w_llc_acc) if w_llc_acc > 0 else None

    perf.append("-" * 60)
    perf.append(f"Benchmark:      {bench_name}")
    perf.append(f"EffectiveWeight:{w_sum:.4f}")
    perf.append(f"Weighted IPC:   {ipc:.4f}")
    perf.append(f"L1I Hit Rate:   {l1i_hr:.2f} %")
    perf.append(f"L1D Hit Rate:   {l1d_hr:.2f} %")
    if llc_hr is None:
        perf.append("LLC Hit Rate:   N/A")
    else:
        perf.append(f"LLC Hit Rate:   {llc_hr:.2f} %")
    perf.append("=" * 60)
    return status, perf


def main():
    ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    status_lines = [
        f"Log Status Report @ {ts}",
        f"LOG_ROOT_DIR = {os.path.abspath(LOG_ROOT_DIR)}",
        f"WEIGHTS_DIR  = {os.path.abspath(WEIGHTS_DIR)}",
        "",
    ]
    perf_lines = [
        f"Performance Report @ {ts}",
        f"LOG_ROOT_DIR = {os.path.abspath(LOG_ROOT_DIR)}",
        "",
    ]

    if not os.path.isdir(LOG_ROOT_DIR):
        status_lines.append("Error: LOG_ROOT_DIR not found.")
        os.makedirs(LOG_ROOT_DIR, exist_ok=True)
        with open(LOG_STATUS_REPORT, "w", encoding="utf-8") as f:
            f.write("\n".join(status_lines) + "\n")
        print(f"Wrote status report: {os.path.abspath(LOG_STATUS_REPORT)}")
        return

    if not os.path.isdir(WEIGHTS_DIR):
        status_lines.append("Error: WEIGHTS_DIR not found.")
        with open(LOG_STATUS_REPORT, "w", encoding="utf-8") as f:
            f.write("\n".join(status_lines) + "\n")
        print(f"Wrote status report: {os.path.abspath(LOG_STATUS_REPORT)}")
        return

    bench_dirs = sorted(
        d for d in glob.glob(os.path.join(LOG_ROOT_DIR, "*")) if os.path.isdir(d)
    )
    status_lines.append(f"[INFO] benchmark dirs found: {len(bench_dirs)}")
    if not bench_dirs:
        status_lines.append("[WARN] No benchmark directories under LOG_ROOT_DIR.")

    for d in bench_dirs:
        st, pf = process_benchmark(d)
        status_lines.extend(st)
        perf_lines.extend(pf)

    with open(LOG_STATUS_REPORT, "w", encoding="utf-8") as f:
        f.write("\n".join(status_lines) + "\n")
    with open(PERF_REPORT, "w", encoding="utf-8") as f:
        f.write("\n".join(perf_lines) + "\n")

    print(f"Wrote status report: {os.path.abspath(LOG_STATUS_REPORT)}")
    print(f"Wrote perf report:   {os.path.abspath(PERF_REPORT)}")


if __name__ == "__main__":
    main()
