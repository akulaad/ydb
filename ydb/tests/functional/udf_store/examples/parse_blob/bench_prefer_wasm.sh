#!/usr/bin/env bash
#
# A/B timing of the PreferWasm path against a running cluster: same binary, same
# data, the only difference is
#
#   PRAGMA ydb.EnableWasmUdfResidentStringColumns = "true" | "false"
#
# "true"  - the scan writes the blob column straight into the query compartment,
#           and the UDF argument reuses those bytes;
# "false" - the scan builds a host string and every UDF call copies it into the
#           compartment.
#
# Prerequisites: a running database with the ParseBlob UDF uploaded (see
# ../../test_udf_store.py and upload_udf), and the ydb CLI in PATH.
#
# Note on the query shape: each row feeds its blob to exactly one UDF call,
# because YQL collapses repeated identical calls into one. At one call per value
# both paths do a single guest malloc/copy/free, so the measured difference stays
# inside the run-to-run noise (a few percent); the resident path pulls ahead only
# when one value reaches several UDF calls.

set -euo pipefail

YDB=${YDB:-ydb}
ENDPOINT=${ENDPOINT:-grpc://localhost:31011}
DB=${DB:-/Root/test}
TABLE=${TABLE:-parse_blob_bench}
ROWS=${ROWS:-2048}           # rounded up to a power of two
BLOB_SIZE=${BLOB_SIZE:-65536}
RUNS=${RUNS:-7}
SKIP_SEED=${SKIP_SEED:-0}

STATS_DIR=$(mktemp -d)
trap 'rm -rf "$STATS_DIR"' EXIT

# Via a file: a seeding query carries a whole blob literal and blows the argv limit.
run_sql() {
    printf '%s' "$1" >"$STATS_DIR/query.sql"
    "$YDB" -e "$ENDPOINT" -d "$DB" sql -f "$STATS_DIR/query.sql" >/dev/null
}

seed() {
    echo "seeding $TABLE: >= $ROWS rows x $BLOB_SIZE bytes"
    run_sql "DROP TABLE IF EXISTS $TABLE;"
    run_sql "CREATE TABLE $TABLE (id Uint64, blob String, PRIMARY KEY (id));"

    local blob
    blob=$(head -c "$BLOB_SIZE" /dev/zero | tr '\0' 'a')
    run_sql "UPSERT INTO $TABLE (id, blob) VALUES (0ul, \"$blob\");"

    # Doubling keeps the seeding queries small: one row of SQL text, log2(ROWS) queries.
    local rows=1
    while ((rows < ROWS)); do
        run_sql "UPSERT INTO $TABLE (id, blob) SELECT id + ${rows}ul AS id, blob FROM $TABLE;"
        rows=$((rows * 2))
    done
    echo "seeded $rows rows"
}

make_query() {
    local enabled=$1
    cat <<EOF
PRAGMA ydb.EnableWasmUdfResidentStringColumns = "$enabled";
SELECT SUM(CAST(LENGTH(ParseBlob::parse_blob(blob)) AS Uint64)) AS checksum
FROM $TABLE;
EOF
}

# "<wall ms> <server cpu us>" for one query; full stats go to $STATS_DIR.
# Wall time carries the CLI startup, so the server CPU time is the sharper number.
timed_run() {
    local enabled=$1 tag=$2
    local start end cpu
    start=$(date +%s%N)
    "$YDB" -e "$ENDPOINT" -d "$DB" sql -s "$(make_query "$enabled")" --stats full \
        >"$STATS_DIR/$tag.txt" 2>&1
    end=$(date +%s%N)
    cpu=$(grep -m1 -oE '^total_cpu_time_us: [0-9]+' "$STATS_DIR/$tag.txt" | grep -oE '[0-9]+' || echo 0)
    echo "$(( (end - start) / 1000000 )) ${cpu:-0}"
}

median() {
    sort -n | awk '{v[NR]=$1} END {print (NR % 2) ? v[(NR+1)/2] : int((v[NR/2]+v[NR/2+1])/2)}'
}

summarize() {
    local file=$1 wall cpu
    wall=$(cut -d' ' -f1 "$file" | median)
    cpu=$(cut -d' ' -f2 "$file" | median)
    echo "$wall $cpu"
}

if ((SKIP_SEED == 0)); then
    seed
fi

echo "measuring $RUNS runs per mode (plus a warmup), modes interleaved"
timed_run true warmup-true >/dev/null
timed_run false warmup-false >/dev/null
# Interleaved so that cache/page warmup drift hits both modes equally.
for ((run = 1; run <= RUNS; ++run)); do
    timed_run true "true-$run" >>"$STATS_DIR/samples-true"
    timed_run false "false-$run" >>"$STATS_DIR/samples-false"
done

read -r resident_ms resident_cpu < <(summarize "$STATS_DIR/samples-true")
read -r host_ms host_cpu < <(summarize "$STATS_DIR/samples-false")

echo
echo "rows=$ROWS blob=$BLOB_SIZE runs=$RUNS (medians)"
printf '  resident (PreferWasm on):  %6s ms wall, %8s us cpu\n' "$resident_ms" "$resident_cpu"
printf '  host     (PreferWasm off): %6s ms wall, %8s us cpu\n' "$host_ms" "$host_cpu"
awk -v aw="$resident_ms" -v bw="$host_ms" -v ac="$resident_cpu" -v bc="$host_cpu" 'BEGIN {
    if (bw > 0) printf "  delta wall: %+.1f%%\n", 100.0 * (aw - bw) / bw
    if (bc > 0) printf "  delta cpu:  %+.1f%%\n", 100.0 * (ac - bc) / bc
}'
echo
echo "per-stage statistics of the last runs:"
echo "  $STATS_DIR/true-$RUNS.txt, $STATS_DIR/false-$RUNS.txt"
trap - EXIT
