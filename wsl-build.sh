#!/bin/bash
# One-shot VitaSDK + VPK build (run inside WSL Ubuntu).
# Usage: bash wsl-build.sh  (needs sudo, ~2GB download, 10-20 min first run)
set -e
if [ ! -d /usr/local/vitasdk ]; then
  echo "[*] Installing VitaSDK prebuilt..."
  sudo mkdir -p /usr/local/vitasdk
  URL=$(curl -sL https://api.github.com/repos/vitasdk/autobuilds/releases/latest \
    | grep browser_download_url | grep -i linux | head -1 | cut -d'"' -f4)
  echo "URL: $URL"
  curl -sL "$URL" -o /tmp/vitasdk.tar.bz2
  sudo tar -xjf /tmp/vitasdk.tar.bz2 -C /usr/local/vitasdk --strip-components=1
fi
export VITASDK=/usr/local/vitasdk
export PATH=$VITASDK/bin:$PATH
sudo apt-get update -y && sudo apt-get install -y cmake git python3 curl
if [ ! -f $VITASDK/bin/vdpm ]; then echo "vdpm missing, toolchain incomplete"; exit 1; fi
vdpm curl openssl || true
SRC=$(dirname "$0")
cmake -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake -B /tmp/vita-plex-build -S "$SRC"
cmake --build /tmp/vita-plex-build
echo "VPK files:"
find /tmp/vita-plex-build -maxdepth 1 -name "vita-plex-client-*.vpk" -print
