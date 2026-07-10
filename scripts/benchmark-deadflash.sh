#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'TXT'
USAGE
    benchmark-deadflash.sh DEADFLASH IMAGE TARGET [RUNS] [VERIFY] [TOKEN]

EXAMPLE
    ./scripts/benchmark-deadflash.sh ./build/deadflash image.iso target.img 5 full

For a physical device, pass the exact token as argument six. The script then
adds --allow-device automatically.
TXT
}

if [[ $# -lt 3 || $# -gt 6 ]]; then
    usage >&2
    exit 2
fi

exe=$1
image=$2
target=$3
runs=${4:-5}
verify=${5:-full}
token=${6:-}
out_dir=${DEADFLASH_BENCH_DIR:-deadflash-benchmark}

mkdir -p "$out_dir"
image_hash=$(sha256sum "$image" | awk '{print $1}')

printf 'run,state,write_mib_s,total_ms,write_ms,flush_ms,verify_ms,bytes_written,bytes_verified,retries,mismatches\n' > "$out_dir/runs.csv"

for ((run = 1; run <= runs; run++)); do
    report=$(printf '%s/run-%02d.json' "$out_dir" "$run")
    args=(write "$image" "$target" --verify "$verify" --report "$report")
    if [[ -n "$token" ]]; then
        args+=(--allow-device --confirm "$token")
    fi
    "$exe" "${args[@]}"
    python3 - "$report" "$run" "$image_hash" >> "$out_dir/runs.csv" <<'PY'
import csv
import json
import sys

path, run, expected_hash = sys.argv[1:]
with open(path, 'r', encoding='utf-8') as f:
    record = json.load(f)
if record['result']['source_sha256'] != expected_hash:
    raise SystemExit(f"source hash mismatch in {path}")
r = record['result']
writer = csv.writer(sys.stdout, lineterminator='\n')
writer.writerow([
    run, r['state'], r['write_mib_s'], r['total_ms'], r['write_ms'],
    r['flush_ms'], r['verify_ms'], r['bytes_written'], r['bytes_verified'],
    r['write_retries'], r['verification_mismatches']
])
PY
done

python3 - "$out_dir/runs.csv" "$out_dir/summary.json" "$image_hash" "$target" "$verify" <<'PY'
import csv
import json
import statistics
import sys
from datetime import datetime, timezone

csv_path, out_path, image_hash, target, verify = sys.argv[1:]
with open(csv_path, newline='', encoding='utf-8') as f:
    rows = list(csv.DictReader(f))
rates = [float(r['write_mib_s']) for r in rows]
totals = [float(r['total_ms']) for r in rows]

def stats(values):
    return {
        'minimum': min(values),
        'median': statistics.median(values),
        'maximum': max(values),
        'mean': statistics.fmean(values),
        'sample_stddev': statistics.stdev(values) if len(values) > 1 else 0.0,
    }

states = {}
for row in rows:
    states[row['state']] = states.get(row['state'], 0) + 1
summary = {
    'schema': 'deadflash.benchmark.summary.v1',
    'created_utc': datetime.now(timezone.utc).isoformat(),
    'image_sha256': image_hash,
    'target': target,
    'verify_mode': verify,
    'runs': len(rows),
    'write_mib_s': stats(rates),
    'total_ms': stats(totals),
    'states': [{'state': state, 'count': count} for state, count in sorted(states.items())],
}
with open(out_path, 'w', encoding='utf-8') as f:
    json.dump(summary, f, indent=2)
print(json.dumps(summary, indent=2))
PY
