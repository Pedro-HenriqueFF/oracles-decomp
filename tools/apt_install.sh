#!/bin/sh
# apt-get update and install with time limits and retries. A package mirror that stalls on a CI
# runner otherwise holds the job until GitHub's six-hour limit (it did on 2026-10-23, for every
# windows-cross job one afternoon). Each try is short enough that all three fit in a 30-minute job,
# and a retry leaves the runner's own mirror (azure.archive.ubuntu.com) for Ubuntu's main archive.
# usage: tools/apt_install.sh PACKAGE...
set -e
sudo=$(command -v sudo || true)
opts="-o Acquire::Retries=3 -o Acquire::http::Timeout=30 -o Acquire::https::Timeout=30 -o DPkg::Lock::Timeout=60"
for attempt in 1 2 3; do
  if ! $sudo timeout 120 apt-get $opts update; then
    echo "::warning::apt-get update attempt $attempt failed or took over 2 minutes"
  elif $sudo timeout 300 apt-get $opts install -y --no-install-recommends "$@"; then
    exit 0
  else
    echo "::warning::apt-get install attempt $attempt failed or took over 5 minutes"
  fi
  $sudo dpkg --configure -a || true
  for f in /etc/apt/sources.list /etc/apt/sources.list.d/ubuntu.sources; do
    if [ -f "$f" ]; then $sudo sed -i 's|azure\.archive\.ubuntu\.com|archive.ubuntu.com|g' "$f"; fi
  done
  sleep 10
done
echo "::error::apt-get could not install $*"
exit 1
