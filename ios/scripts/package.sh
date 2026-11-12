#!/usr/bin/env bash
# Monta o BT3.ipa (sem assinatura: o SideStore assina) a partir do que o link deixou em port/build.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

APP=ipa/Payload/BT3.app
rm -rf ipa icon BT3.ipa
mkdir -p "$APP" icon

cp port/build/bt3_ios "$APP/bt3_ios"
# os arquivos que o motor procura ao lado do executavel
for ext in dat mem; do
  if [ -f "port/build/bt3_ios.$ext" ]; then cp "port/build/bt3_ios.$ext" "$APP/bt3_ios.$ext"; else echo "::warning::bt3_ios.$ext nao foi gerado"; fi
done
cp -R port/translations "$APP/translations"
cp ios/scripts/Info.plist "$APP/Info.plist"

# o icone: gerado aqui (nada de arte do jogo), em 120 e 180 pixels
PY="${PY:-.venv/bin/python}"
[ -x "$PY" ] || PY=python3
"$PY" -m pip install --quiet --disable-pip-version-check pillow
"$PY" ios/scripts/make_icon.py icon
cp icon/AppIcon60x60@2x.png icon/AppIcon60x60@3x.png "$APP/"

(cd ipa && zip -qr ../BT3.ipa Payload)
ls -la BT3.ipa "$APP"
