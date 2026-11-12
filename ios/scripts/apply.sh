#!/usr/bin/env bash
# Aplica o port para iOS ao projeto: os patches de ios/patches (arquivos compartilhados) e as copias dos arquivos
# novos de ios/src (que so o iOS compila; ficam fora de port/src para nao entrarem nos builds das outras plataformas).
# Pode ser rodado de novo sem estrago.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

if grep -q '"ios64"' port/tools/toolchain.py; then
  echo "os patches ja estao aplicados neste projeto"
else
  for p in ios/patches/*.patch; do
    if git apply --check "$p" 2>/dev/null; then
      git apply "$p"
      echo "aplicado: $p"
    else
      echo "::error::o patch $p nao se aplica a este projeto (a versao dos arquivos e outra?)"
      git apply --check "$p"
      exit 1
    fi
  done
fi

install -m 0644 ios/src/plat_ios.c port/src/plat_ios.c
for f in ios_setup.c ios_setup.h ios_setup_ui.c ios_touch.c; do
  install -m 0644 "ios/src/gs/$f" "port/src/gs/$f"
done
echo "arquivos do iOS copiados para port/src"
# conferencia: a variante ios64 existe (precisa do xcrun, so no macOS)
BT3_CC=ios64 python3 -c "import sys; sys.path.insert(0,'port/tools'); import toolchain as t; print(t.VARIANT, t.MAC, t.IOS, t.MAC_TRIPLE, t.EXE, t.CLANG_TARGET)" || echo "(conferencia da variante ignorada: sem xcrun)"
