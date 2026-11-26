#!/bin/bash
# One-time setup of a fresh Ubuntu 24.04 server. Run as root:
#   curl -fsSL https://raw.githubusercontent.com/Bryan438/Texas_Holdem_1/main/deploy/setup.sh -o setup.sh
#   sudo bash setup.sh poker.example.com      # with a domain: HTTPS
#   sudo bash setup.sh                        # without one: plain http://SERVER_IP
set -euo pipefail
DOMAIN="${1:-}"
REPO=https://github.com/Bryan438/Texas_Holdem_1.git

if [ "$(id -u)" -ne 0 ]; then
  echo "Run as root: sudo bash setup.sh [domain]"
  exit 1
fi

echo "== Swap: building on a 1 GB machine needs a little more memory"
if ! swapon --show | grep -q .; then
  fallocate -l 1G /swapfile
  chmod 600 /swapfile
  mkswap /swapfile >/dev/null
  swapon /swapfile
  grep -q '^/swapfile' /etc/fstab || echo '/swapfile none swap sw 0 0' >> /etc/fstab
fi

echo "== Packages"
apt-get update -q
DEBIAN_FRONTEND=noninteractive apt-get install -y -q build-essential git caddy ufw

echo "== A user for the game, and its folders"
id poker >/dev/null 2>&1 || useradd --system --home /var/lib/thserver --shell /usr/sbin/nologin poker
install -d -m 755 /opt/thserver
install -d -m 700 -o poker -g poker /var/lib/thserver   # hands.jsonl: every player's cards

echo "== The code"
if [ -d /opt/thserver/src/.git ]; then
  git -C /opt/thserver/src pull --quiet
else
  git clone --quiet "$REPO" /opt/thserver/src
fi

echo "== Build, install, start"
bash /opt/thserver/src/deploy/update.sh
systemctl enable --quiet thserver

echo "== Caddy: HTTPS in front of the game"
if [ -n "$DOMAIN" ]; then
  SITE="$DOMAIN"
else
  SITE=":80"
fi
cat > /etc/caddy/Caddyfile <<CADDY
$SITE {
	encode gzip
	reverse_proxy 127.0.0.1:8080
}
CADDY
systemctl reload caddy || systemctl restart caddy

echo "== Firewall: SSH and the web only"
ufw allow OpenSSH >/dev/null
ufw allow 80/tcp >/dev/null
ufw allow 443/tcp >/dev/null
ufw --force enable >/dev/null
ufw status

echo
if [ -n "$DOMAIN" ]; then
  echo "Done. Open https://$DOMAIN (the certificate may take a minute the first time)."
else
  echo "Done. Open http://$(curl -fsS https://api.ipify.org 2>/dev/null || echo SERVER_IP)"
fi
echo "Console: sudo thctl tables | players | kick NAME [TABLE] | close TABLE"
echo "Update later: sudo /opt/thserver/src/deploy/update.sh"
