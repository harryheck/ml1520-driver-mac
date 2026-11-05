#!/bin/bash
# Tests ml1520-print without a printer. tests/fixtures/page.pwg is a real job as sent by macOS,
# and expected.qpdl is what SpliX 2.0.2 rastertoqpdl makes of it. SpliX only needs one change
# to read such a job: cupsCompression set to 17 in the page header. The output must match byte
# for byte, apart from the date in the PJL header.
set -euo pipefail
cd "$(dirname "$0")/.."

[ -x build/ml1520-print ] || ./build.sh
mkdir -p build/test

IPP_JOB_ORIGINATING_USER_NAME=test IPP_JOB_NAME=test \
    build/ml1520-print --output build/test/actual.qpdl tests/fixtures/page.pwg 2>/dev/null

python3 -I -c '
import re, sys
strip = lambda path: re.sub(rb"SERVICEDATE=\d{8}", b"", open(path, "rb").read(), count=1)
sys.exit(strip(sys.argv[1]) != strip(sys.argv[2]))
' build/test/actual.qpdl tests/fixtures/expected.qpdl || {
    echo "FAILED: output differs from SpliX" >&2; exit 1; }
echo "OK: output matches SpliX byte for byte"

python3 -I tools/decode-qpdl.py build/test/actual.qpdl build/test/page
