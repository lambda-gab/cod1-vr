# Informações técnicas

[English](TECHNICAL.md) | **Português** · [Voltar ao README](../README.pt-BR.md)

## Como funciona

O mod é um `opengl32.dll` proxy colocado ao lado do `CoDSP.exe`. Ele repassa todas as chamadas OpenGL para o
`opengl32.dll` do sistema e intercepta o renderizador do jogo (id Tech 3 modificado) para:

- reativar o caminho de estéreo do motor e desenhar cada vista do mundo uma vez por olho, com o FOV assimétrico de
  cada olho e o IPD do runtime (passe único: a cena é montada uma vez e desenhada para os dois olhos);
- enviar os olhos para o headset via OpenXR (`XR_KHR_opengl_enable`), com o HUD e os menus numa camada quad e o laser
  dos menus e a trajetória da granada numa camada extra;
- levar a arma da vista e o clarão do disparo para a mão rastreada, escrever a mira nos ângulos de visão do jogo,
  somar o movimento analógico e o roomscale ao usercmd e mapear os botões do controle para comandos do jogo;
- mostrar os botões do controle nos avisos de tecla do jogo (um detour no `cgamex86.dll`, refeito a cada fase).

Ele só é ativado no `CoDSP.exe` 1.5 (o timestamp do PE é conferido) e fica inativo nos demais casos. Foi feito a
partir de engenharia reversa do `CoDSP.exe` com [REA](https://github.com/morluto/rea) + Ghidra, tendo como referência
o [RTCWQuest](https://github.com/Team-Beef-Studios/RTCWQuest). A documentação completa dos endereços, hooks e decisões
está em [`CoD_VR_RE.md`](CoD_VR_RE.md).

Sem headset conectado, o mod mostra os dois olhos lado a lado no monitor.

## Pacotes de dados opcionais (instalação manual)

O instalador os gera a partir da sua própria cópia do jogo. Para fazer à mão:

```
python mods/build_pk3.py "<pasta do jogo>"
```

Depois copie de `mods/out/` para `<pasta do jogo>\main`:

- `pakz_vr_nohands.pk3`: esconde as mãos em primeira pessoa, deixando só a arma na sua mão.
- `pakz_vr_text.pk3`: as duas frases do treinamento que falam de mouse, reescritas para o VR.

O CoD 1.5 só carrega pacotes com nome `pak*`. Apague um pacote para desfazer.

## Linha de comando do instalador

`CoD-VR-Setup.exe /silent /dir="<pasta do jogo>" [/hands]` ou `/silent /dir="..." /uninstall`.

Nas atualizações o `cod_vr.ini` é mantido e os novos padrões vão para `cod_vr.ini.default`. Outro `opengl32.dll` que
já esteja na pasta ganha um backup.

## Log e locais dos arquivos

Se a pasta do jogo estiver em `Program Files`, copiar arquivos para lá precisa de administrador. O log fica em
`cod_vr.log`; com a virtualização de arquivos do Windows, ele vai para `%LOCALAPPDATA%\VirtualStore\...`. Com
`LogNearEntities=1` em `[Debug]`, o log registra as entidades adicionadas perto da vista quando você aperta o gatilho
(usado para achar efeitos como o clarão do disparo).

## Compilar

Precisa de Visual Studio com as ferramentas C++ (o build usa o compilador x86) e do loader OpenXR da Khronos:

1. Baixe `openxr_loader_windows-<versão>.zip` em
   [OpenXR-SDK-Source releases](https://github.com/KhronosGroup/OpenXR-SDK-Source/releases)
   e extraia em `deps/openxr/` (ficando `deps/openxr/include` e `deps/openxr/Win32`).
2. Rode `proxy\build.bat`. A saída vai para `proxy\out\` (`opengl32.dll` e `openxr_loader.dll`).
3. Opcional: `installer\build_setup.bat` gera o `installer\out\CoD-VR-Setup.exe`, com esses arquivos embutidos.

Ao enviar uma tag como `v1.1.0`, o workflow do GitHub Actions (`.github/workflows/release.yml`) compila tudo e
publica uma release com o instalador e um zip para instalação manual.

O instalador usa a [miniz](https://github.com/richgel999/miniz) (MIT, em `third_party/miniz`) para ler os `.pk3`
do jogo.

`proxy/stubs.c` e `proxy/opengl32.def` são gerados por `proxy/gen_stubs.py` a partir da lista de exports do
`opengl32.dll` de 32 bits do sistema (`proxy/ogl_exports.txt`).

Testes fora do jogo: `proxy/test/build_test.bat` (`gltest`: carga do proxy e passthrough) e
`proxy/test/build_xrtest.bat` (`xrtest`: sessão OpenXR, olhos coloridos, HUD e leitura dos controles).

## Ferramentas de engenharia reversa

`tools/` tem os scripts usados na investigação:

- `reacli.mjs`: cliente MCP mínimo que mantém uma sessão do REA/Ghidra aberta e executa uma lista de consultas.
  Precisa do REA (`npm install -g rea-agents`), de `GHIDRA_INSTALL_DIR` e de `JAVA_HOME`.
- `dis.py`: desmonta trechos com o capstone.
- `scan.py`, `off2va.py`, `pe.py`, `callscan.py`: strings, offsets e imports/exports.
- `pk3scan.py`, `pk3find.py`: inspeção dos `.pk3` do jogo (com `COD_DIR` apontando para a pasta do jogo).

Nada da saída do Ghidra (pseudocódigo do jogo) está no repositório.
