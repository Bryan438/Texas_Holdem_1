# Running thserver on the internet

One small Linux server (1 CPU, 1 GB of memory is plenty), Ubuntu 24.04 LTS.
[Caddy](https://caddyserver.com) sits in front: it answers on ports 80 and 443,
gets and renews a free HTTPS certificate for your domain, and passes browsers
(the page and its WebSocket) on to thserver, which listens on this machine only.

```
browser ──https──> Caddy :443 ──> thserver 127.0.0.1:8080
```

## What you need

- A server with Ubuntu 24.04 and your SSH key on it.
- A domain (optional but recommended): an `A` record, e.g. `poker.example.com`,
  pointing at the server's IP address. Without one the game is plain
  `http://SERVER_IP`, unencrypted.

## Set it up (once)

```bash
ssh root@SERVER_IP
curl -fsSL https://raw.githubusercontent.com/Bryan438/Texas_Holdem_1/main/deploy/setup.sh -o setup.sh
less setup.sh                       # read what it does first
bash setup.sh poker.example.com     # or: bash setup.sh   (no domain)
```

`setup.sh`:
- adds 1 GB of swap if there is none;
- installs `build-essential`, `git`, `caddy` and `ufw`;
- creates a `poker` user with no login;
- clones this repository into `/opt/thserver/src`, builds `thserver`, and installs it with
  the web page into `/opt/thserver`;
- installs the `thserver` service (systemd: starts at boot, restarts if it crashes);
- writes `/etc/caddy/Caddyfile`;
- opens only SSH, 80 and 443 in the firewall.

Hands are saved to `/var/lib/thserver/hands.jsonl`, which only the `poker` user can
read: it holds every player's cards.

## Day to day

```bash
sudo thctl tables                    # the open tables
sudo thctl players                   # who sits where
sudo thctl kick Ann 3                # remove Ann from table 3
sudo journalctl -u thserver -f       # follow the log
sudo systemctl restart thserver      # restart (everyone reconnects; games start over)
sudo /opt/thserver/src/deploy/update.sh   # install the latest main and restart
```

## What protects it

- thserver listens on 127.0.0.1 only; the outside reaches it through Caddy.
- At most 200 open connections in all and 20 per address (`--max-connections`,
  `--max-per-ip`). Behind Caddy, browsers are counted by their real address
  (`--trust-proxy` reads the `X-Forwarded-For` header Caddy sets).
- Messages are capped at 64 KB, a silent HTTP request is dropped after 10 s, and TCP
  keepalive notices peers that vanish.
- The service runs as `poker`, can write only to `/var/lib/thserver`, and gets no new
  privileges (see `thserver.service`).
- Private tables have a password. There are no accounts yet: anyone with the link can
  open the lobby and sit at a public table.

## The terminal client

The TCP port (4000) carries no encryption, so it stays closed: `--bind 127.0.0.1` in
`thserver.service`, and the firewall does not open it. To let terminal clients in,
change that to `--bind 0.0.0.0`, run `sudo ufw allow 4000/tcp`, then `sudo systemctl daemon-reload` and
`sudo systemctl restart thserver`. Anyone who can watch the network between a player and
the server can then read that player's messages.
