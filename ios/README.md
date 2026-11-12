# Port do Tenkaichi3Decomp para iOS

O jogo do projeto (Budokai Tenkaichi 3, a decompilação em C) rodando num iPhone, compilado no GitHub Actions (sem Mac)
e instalado pelo SideStore. Esta pasta tem **tudo o que é específico do iOS**, separado do resto do projeto:

```
ios/
  src/              arquivos novos (só o iOS os compila; o apply.sh os copia para port/src)
    plat_ios.c        a entrada do app, a janela de 4 GB, o espelho do programa, o log
    gs/ios_setup.c    o disco (.iso) para gamedata/: lê ISO 9660 e divide os AFS (sem tela)
    gs/ios_setup.h    a estrutura de progresso, compartilhada com a tela
    gs/ios_setup_ui.c a tela de preparação dos dados (UIKit, seletor de arquivos)
    gs/ios_touch.c    o controle na tela (UIKit): analógico, botões, menu da engrenagem
  patches/          as mudanças em arquivos que já existiam (git apply, em ordem)
  scripts/          apply.sh (aplica), package.sh (monta o BT3.ipa), make_icon.py, Info.plist
```

## Como funciona (resumo)

- **A janela de 4 GB.** Os endereços do jogo são deslocamentos numa janela de 4 GB cuja base tem os 32 bits baixos
  zerados. No macOS o programa é ligado em 1 TB e se reinicia sem ASLR para ficar lá; no iOS isso é impossível
  (`posix_spawn` é recusado e o processo só tem uma faixa livre de ~6 GB, que começa em `0x7000000000`). Então
  `plat_ios.c` reserva a janela alinhada, **espelha o próprio programa** nela (`vm_remap`: as mesmas páginas, ainda
  assinadas), move para o espelho os ponteiros que o programa guarda nos dados (os *rebase opcodes* do dyld) e roda o
  jogo a partir do espelho.
- **Entrada.** `-e ___wrap_main` chama o `SDL_RunApp` (UIKit); o jogo começa no `app_main`, na thread principal.
- **Gráficos.** SDL_GPU com Metal; shaders em MSL gerados pelo spirv-cross (`--msl-ios`).
- **Dados.** O motor lê o disco desempacotado em `Documents/gamedata` (o que o `extract_disc.py` faz num PC).
  `ios_setup*.c` faz isso no aparelho, a partir de um `.iso` escolhido no seletor de arquivos ou colocado na pasta do app.
- **Controle.** `ios_touch.c` usa o `Port_TouchPad` que o motor já tem para o Android.
- **Áudio.** Sessão `ambient` (mistura com o que o aparelho já toca).

## Os patches (`ios/patches`)

| patch | o que faz |
|---|---|
| 0001-ios64-toolchain | a variante `ios64` do toolchain: alvo `arm64-apple-ios14.0`, SDK do iPhone, MSL, o link do app |
| 0002-ios-compile-fixes | versões de *availability* do SDK reescritas pelo eeconst, arquivos em Objective-C, erros completos |
| 0003-ios-window-and-entry | `PORT_WINDOW` = `Port_Window`, regiões com `MAP_FIXED`, `Port_GameEntry` e `Port_MapMemory` |
| 0004-metal-msl-device | no Apple o dispositivo SDL_GPU pede shaders MSL, não SPIR-V |
| 0005-ios-audio-session | sessão de áudio do miniaudio (ambient) |
| 0006-memory-card-errors | erros reais do cartão de memória no log |
| 0007-a11y-static-assert | os `static_assert` de layout do leitor de tela, no Apple (libclang 18 não honra `__ptr32` no arm64) |

Os trechos em arquivos compartilhados são protegidos (`#ifdef __APPLE__`, `TARGET_OS_IPHONE`, `IOS`), para não mudarem
os outros builds.

## Compilar

Pelo GitHub Actions: **Actions → ios → Run workflow** (máquina `macos-15`). O workflow instala as ferramentas, roda
`ios/scripts/apply.sh`, compila o SDL3 para iOS, o jogo e o link, e roda `ios/scripts/package.sh`. O resultado é o
artefato `BT3-ipa` e uma cópia em *Releases* (`ios-latest` e `build-<número>`).

## Instalar e usar

- Instale o `BT3.ipa` pelo SideStore **por cima do app** (apagar o app apaga os dados e os saves). A conta gratuita
  expira o app em 7 dias: use *Refresh*. O bundle id reaproveita um App ID já registrado (o limite é de 10 por semana).
- No primeiro uso o app pede o `.iso` do **seu** disco (versão USA, SLUS-21678) e desempacota para `Documents/gamedata`
  (uns 2,8 GB). Nenhum dado do jogo faz parte deste repositório.
- Saves: `Documents/saves/card1/…` (aparece no app Arquivos). Faça backup dessa pasta.
- Log: `Documents/bt3_log.txt` (e `bt3_log_prev.txt`, o da abertura anterior).
- Configurações do motor: `Documents/bt3_settings.txt` (`START + SELECT` abre a janela de configurações). Linhas só do
  iOS: `late_input=1` (atraso de entrada baixo, ligado por padrão), `ios_drawables=2` (imagens na fila da tela; 3 se o jogo
  engasgar), `ios_defaults=1` (marca que os padrões do iOS já foram aplicados).

## Limitações conhecidas

- Com um disco diferente do USA original o motor avisa que há arquivos modificados e podem ocorrer glitches.
- A lógica do jogo lê o controle a cada 33 ms nas lutas (como no PS2): parte do atraso é do próprio jogo.
- O resto da interface (a janela de configurações do motor) é a do desktop.

## Créditos e licença

O motor e a decompilação são de outros autores e de código aberto (GPL): <https://github.com/z3xox/Tenkaichi3Decomp> e
<https://github.com/dfdx047/Tenkaichi3DecompAndroid>. Quem distribuir um build binário deve oferecer o código das
mudanças (esta pasta) e nunca os dados do jogo. Confira a licença no repositório original.
