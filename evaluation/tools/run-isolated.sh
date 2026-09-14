#!/bin/sh
# Trusted fixed argv. Only reviewed jobs may be passed; do not expose in web UI.
set -eu
if [ "$#" -ne 2 ]; then
  echo "usage: run-isolated.sh /absolute/worker /absolute/reviewed-jobs.json" >&2
  exit 2
fi
worker=$(realpath "$1")
jobs=$(realpath "$2")
exec /usr/bin/timeout --signal=KILL 900 /usr/bin/bwrap \
  --unshare-all --die-with-parent --new-session --clearenv --cap-drop ALL \
  --ro-bind /usr /usr --symlink usr/bin /bin \
  --ro-bind /lib /lib --ro-bind /lib64 /lib64 \
  --proc /proc --dev /dev --tmpfs /tmp --size 268435456 --tmpfs /work \
  --ro-bind "$worker" /worker --ro-bind "$jobs" /input.json \
  --ro-bind /dev/null /hy3-isolation-policy \
  --setenv PATH /usr/bin:/bin --setenv LANG C.UTF-8 --chdir /work /worker
