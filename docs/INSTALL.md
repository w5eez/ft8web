# Installing ft8web

Acquire a Raspberry Pi 5 w/Raspberry Pi OS Lite or a reasonably fast X86 Debian system and a hamlib-supported radio with USB CAT and USB audio between the rig and the Pi.

ft8web has to live at **`~/ft8web`**.

```bash
cd ~
sudo apt update && sudo apt upgrade && sudo apt install -y git
git clone <repo-url>
cd ~/ft8web
bash scripts/install.sh
```

***This will take a while, be patient!***

When it finishes, start the service and open the UI:

```bash
sudo systemctl restart ft8web
```
Browse to **http://ft8web.local:8080/** (or `http://<pi-ip>:8080/`) and set your callsign, grid, and radio under Settings using the gear, top right.

- Updating later: `git pull && bash scripts/install.sh && sudo systemctl restart ft8web`.

### ft8web.local

The installer sets avahi's mDNS name to `ft8web` (`host-name=ft8web` in the `[server]` block of `/etc/avahi/avahi-daemon.conf`).

```bash
sudo sed -i 's/^host-name=.*/host-name=ft8web-shack/' /etc/avahi/avahi-daemon.conf
sudo systemctl restart avahi-daemon
```

### Live LoTW logging (optional)

You need to install TQSL on the Pi and import your cert in order for LoTW logging to work. Find a guide or ask an LLM.

### GPS (optional/recommended)

edit `/etc/default/gpsd` with module specifics. The defaults should work for most dongles.

```bash
sudo systemctl restart gpsd
```

### Shut Down Pi button

The Settings->Station->Shut Down Pi button powers off the host with `sudo systemctl poweroff`. The installer grants the ft8web service user passwordless sudo for that single command via `/etc/sudoers.d/ft8web`:

```
youruser ALL=(root) NOPASSWD: /usr/bin/systemctl poweroff
```

If you installed before this was added, re-run `bash scripts/install.sh` or add the rule yourself with `sudo visudo -f /etc/sudoers.d/ft8web`, using the `User=` value from `/etc/systemd/system/ft8web.service` and the path from `command -v systemctl`.
