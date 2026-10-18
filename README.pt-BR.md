![Call of Duty VR](docs/images/banner.jpg)

# Call of Duty (2003) VR

[English](README.md) | **Português**

Mod de VR para o **Call of Duty 1 single player** (`CoDSP.exe` 1.5) no PC, via OpenXR. É um `opengl32.dll`
proxy que reativa o caminho de estéreo do motor (id Tech 3 modificado) e envia a imagem para o headset, com
rastreamento de cabeça e mãos, arma presa ao controle, HUD numa tela flutuante e menus com apontador laser.

Feito a partir de engenharia reversa do `CoDSP.exe` (com [REA](https://github.com/morluto/rea) + Ghidra),
tendo como referência o [RTCWQuest](https://github.com/Team-Beef-Studios/RTCWQuest). A documentação completa
dos endereços, hooks e decisões está em [`docs/CoD_VR_RE.md`](docs/CoD_VR_RE.md) (em português).

> **Aviso:** projeto não oficial de fã, sem vínculo com a Activision nem endossado por ela. Call of Duty é uma
> marca registrada da Activision Publishing, Inc. É necessária uma cópia legítima do jogo; nenhum código ou
> arquivo do jogo é incluído ou distribuído (o pacote opcional que esconde as mãos é gerado no seu PC a partir
> da sua própria cópia).

## O que funciona

- Imagem estéreo no headset, com FOV assimétrico por olho, IPD do runtime e 6DoF.
- Roomscale: o personagem acompanha a sua posição real (inclinar na janela, espiar esquinas).
- Mira e arma pelo controle (o viewmodel vai para a mão), empunhadura com as duas mãos.
- Movimento analógico, snap turn ou giro suave, botões mapeáveis para comandos do jogo.
- HUD e menus numa tela flutuante; efeitos de tela cheia (dano, fades) cobrem a visão.
- Luneta: com zoom, a vista ampliada do jogo aparece atrás do overlay da luneta.
- Menus: aponte com a mão da arma; gatilho clica, B volta, A confirma.
- Sem headset conectado, o mod mostra os dois olhos lado a lado no monitor.

## Requisitos

- `CoDSP.exe` **1.5** (o mod confere o timestamp do executável e não faz nada em outras versões ou no `CoDMP.exe`).
- Um runtime OpenXR **de 32 bits**: Virtual Desktop (VDXR) ou Oculus/Meta. O jogo é um processo 32 bits.
- Headset de PC VR com controles (testado com Quest via Virtual Desktop).

O mod roda em PC VR. Quem tem Quest pode jogar sem fio pelo Virtual Desktop ou pelo Air Link.

## Instalação

### Mais fácil: o instalador (recomendado)

Baixe o `CoD-VR-Setup.exe` da
[última release](https://github.com/lambda-gab/cod1-vr/releases/latest) e execute. Ele acha a pasta do jogo
(ou deixa você escolher), confere se o `CoDSP.exe` é a 1.5, instala o mod, faz backup de outro `opengl32.dll`
que já esteja lá e pergunta se você quer esconder as mãos do personagem (os modelos são gerados a partir dos
arquivos do seu jogo; nada do jogo é distribuído). Execute de novo para atualizar ou desinstalar. Nas
atualizações o seu `cod_vr.ini` é mantido (os novos padrões vão para `cod_vr.ini.default`).

Para scripts: `CoD-VR-Setup.exe /silent /dir="<pasta do jogo>" [/hands]` ou `/silent /dir="..." /uninstall`.

### Manual

1. Compile (abaixo) ou extraia o `cod-vr-manual-install.zip` de uma release.
2. Copie para a pasta do jogo (onde está o `CoDSP.exe`): `opengl32.dll`, `openxr_loader.dll` e `cod_vr.ini`.
3. Opcional, para esconder as mãos e deixar só a arma: gere e copie o pacote de dados:

   ```
   python mods/build_pk3.py "<pasta do jogo>"
   ```

   Depois copie `mods/out/pakz_vr_nohands.pk3` para `<pasta do jogo>\main`. O CoD 1.5 só carrega
   pacotes com nome `pak*`.
4. Conecte o headset e abra o `CoDSP.exe`.

Se a pasta do jogo estiver em `Program Files`, a cópia precisa de administrador. O log fica em
`cod_vr.log`; com a virtualização de arquivos do Windows, ele vai para `%LOCALAPPDATA%\VirtualStore\...`.

Para desinstalar, apague os três arquivos e o `pakz_vr_nohands.pk3`.

## Controles (padrão, editáveis em `[Buttons]`)

| Controle | Ação |
|---|---|
| Gatilho direito | Atirar |
| Grip direito | Usar / pegar |
| Gatilho esquerdo | Mirar (ADS; com luneta, abre o zoom) |
| Grip esquerdo | Segurar a arma com as duas mãos |
| Analógico esquerdo | Andar (relativo à cabeça) |
| Analógico direito | Girar (snap 45°); cima/baixo muda a postura |
| A / B | Pular / recarregar |
| X / Y | Próxima arma / coronhada |
| Clique analógico esquerdo | Objetivos |
| Clique analógico direito | Deitar |
| Menu | Menu do jogo |

## Próximos passos

- **United Offensive** (`CoDUOSP.exe`): planejado. A expansão usa um build mais novo do mesmo motor, e tudo o
  que o mod intercepta também existe nela (inclusive o caminho de estéreo), então o port consiste sobretudo em
  mapear os endereços dela. Por enquanto o mod fica inativo no United Offensive e o jogo roda normalmente.

## Compilar

Precisa de Visual Studio com as ferramentas C++ (o build usa o compilador x86) e do loader OpenXR da Khronos:

1. Baixe `openxr_loader_windows-<versão>.zip` em
   [OpenXR-SDK-Source releases](https://github.com/KhronosGroup/OpenXR-SDK-Source/releases)
   e extraia em `deps/openxr/` (ficando `deps/openxr/include` e `deps/openxr/Win32`).
2. Rode `proxy\build.bat`. A saída vai para `proxy\out\` (`opengl32.dll` e `openxr_loader.dll`).
3. Opcional: `installer\build_setup.bat` gera o `installer\out\CoD-VR-Setup.exe`, com esses arquivos embutidos.

Ao enviar uma tag como `v1.0.0`, o workflow do GitHub Actions (`.github/workflows/release.yml`) compila tudo e
publica uma release com o instalador e um zip para instalação manual.

O instalador usa a [miniz](https://github.com/richgel999/miniz) (MIT, em `third_party/miniz`) para ler os
`.pk3` do jogo.

`proxy/stubs.c` e `proxy/opengl32.def` são gerados por `proxy/gen_stubs.py` a partir da lista de exports do
`opengl32.dll` de 32 bits do sistema (`proxy/ogl_exports.txt`).

Testes fora do jogo: `proxy/test/build_test.bat` (`gltest`: carga do proxy e passthrough) e
`proxy/test/build_xrtest.bat` (`xrtest`: sessão OpenXR, olhos coloridos, HUD e leitura dos controles).

## Configuração

Tudo fica em `cod_vr.ini`, com comentários. Os ajustes mais usados:

- `[Controllers] WeaponOffset*`, `WeaponPitch`, `WeaponYaw`: posição e ângulo da arma na mão.
- `MuzzleOffsetRight/Up`: posição do clarão do disparo.
- `TwoHandGripHeight`, `AimSmoothing`: empunhadura com duas mãos e suavização da mira.
- `[VR] RoomScale`, `PositionalTracking`, `WorldScale`.
- `[HUD]` e `[Scope]`: tamanho e distância das telas.

## Ferramentas de engenharia reversa

`tools/` tem os scripts usados na investigação:

- `reacli.mjs`: cliente MCP mínimo que mantém uma sessão do REA/Ghidra aberta e executa uma lista de consultas.
  Precisa do REA (`npm install -g rea-agents`), de `GHIDRA_INSTALL_DIR` e de `JAVA_HOME`.
- `dis.py`: desmonta trechos com o capstone.
- `scan.py`, `off2va.py`, `pe.py`, `callscan.py`: strings, offsets e imports/exports.
- `pk3scan.py`, `pk3find.py`: inspeção dos `.pk3` do jogo (com `COD_DIR` apontando para a pasta do jogo).

Nada da saída do Ghidra (pseudocódigo do jogo) está no repositório.
