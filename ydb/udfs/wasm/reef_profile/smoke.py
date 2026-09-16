#!/usr/bin/env python3
"""Check the locally built ReefProfile on a running YDB instance."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


def main():
    directory = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ydb", default=os.environ.get(
        "YDB_BIN", str(directory.parents[2] / "apps/ydb/ydb")))
    parser.add_argument("--endpoint", default="grpc://localhost:31011")
    parser.add_argument("--database", default="/Root/test")
    args = parser.parse_args()
    command = [args.ydb, "-e", args.endpoint, "-d", args.database]

    def run(*arguments):
        result = subprocess.run(command + list(arguments), capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stderr)
        return json.loads(result.stdout)

    digest = hashlib.md5()
    with (directory / "libwasm-reef_profile.so").open("rb") as module:
        for chunk in iter(lambda: module.read(1024 * 1024), b""):
            digest.update(chunk)
    description = run("udf", "describe", "--name", "ReefProfile", "--format", "json")
    module = description["module"]
    assert module["md5"] == digest.hexdigest(), "Uploaded module differs from the local build"
    assert module["compile_status"] == "ready", module
    platforms = description["platforms"]
    assert platforms and all(item["status"] == "ready" for item in platforms), platforms

    expected = {
        "UserID": "u1",
        "RequestID": "r1",
        "FeedsOutputCache": {"OutputShowBlockIds": ["b1"]},
    }

    def check_query(query):
        result = run("sql", "--format", "json-unicode", "-s", query)
        if isinstance(result, list):
            assert len(result) == 1, result
            result = result[0]
        assert json.loads(result["ok"]) == expected, result
        assert result["null_on_error"] is None, result
        assert json.loads(result["proto_null"]) == {}, result
        assert json.loads(result["proto_null_str"]) == {}, result
        assert json.loads(result["proto_keys"]) == {"UserID": "u1", "RequestID": "r1"}, result
        assert result["proto_null_on_error"] is None, result

    query = (directory / "query.sql").read_text()
    check_query(query)
    # NBlockCodecs zstd: little-endian ui64 length followed by a zstd frame.
    compressed = "040000000000000028b52ffd20042100000a026231"
    zstd_query = query.replace(
        'String::HexDecode("0a026231")', f'String::HexDecode("{compressed}")'
    ).replace('"null" AS codec', '"zstd_6" AS codec', 1)
    assert zstd_query != query
    check_query(zstd_query)

    failure = subprocess.run(command + [
        "sql", "-s", 'SELECT ReefProfile::ParseReefRequestProfileProto("not-protobuf", NULL);',
    ], capture_output=True, text=True)
    assert failure.returncode != 0, failure.stdout
    assert "Can't parse profile protobuf" in failure.stderr, failure.stderr
    print(json.dumps({
        "name": module["name"], "uid": module["uid"], "md5": module["md5"],
        "status": "ready", "checks": "row, proto, nulls, malformed input, zstd, default error",
    }))


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, RuntimeError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
