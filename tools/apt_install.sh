#!/bin/sh
# apt-get update and install with time limits and retries. A package mirror that stalls on a CI
# runner otherwise holds the job until GitHub's six-hour limit (it did on 2026-10-23, for every
# windows-cross job one afternoon).
# usage: tools/apt_install.sh PACKAGE...
set -e
sudo=$(command -v sudo || true)
opts="-o Acquire::Retries=3 -o Acquire::http::Timeout=30 -o Acquire::https::Timeout=30"
for attempt in 1 2 3; do
  if $sudo timeout 300 apt-get $opts update && $sudo timeout 600 apt-get $opts install -y --no-install-recommends "$@"; then exit 0; fi
  echo "::warning::apt-get attempt $attempt failed or timed out"
  $sudo dpkg --configure -a || true
  sleep 15
done
echo "::error::apt-get could not install $*"
exit 1
