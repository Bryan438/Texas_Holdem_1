#!/bin/bash
# Fetch the latest main, build, install and restart. Run on the server:
#   sudo /opt/thserver/src/deploy/update.sh
# Everyone at a table is disconnected for a moment; their pages reconnect,
# but games in progress start over (the tables live in memory).
set -euo pipefail
SRC=/opt/thserver/src
cd "$SRC"
git fetch --quiet origin
git checkout --quiet main
git reset --quiet --hard origin/main
make -j"$(nproc)" thserver
install -m 755 thserver /opt/thserver/thserver
rm -rf /opt/thserver/web
cp -r web /opt/thserver/web
install -m 755 deploy/thctl /usr/local/bin/thctl
install -m 644 deploy/thserver.service /etc/systemd/system/thserver.service
systemctl daemon-reload
systemctl restart thserver
sleep 1
systemctl --no-pager --lines=5 status thserver
