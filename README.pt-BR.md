![Call of Duty VR](docs/images/banner.jpg)

# CALL OF DUTY (2003) VR

[English](README.md) | **Português**

Mod de PC VR para a **campanha single player do Call of Duty 1** (`CoDSP.exe` 1.5). Jogue o original de 2003 no
headset: você olha e se move com o próprio corpo, a arma fica na sua mão, a mira é pelo controle e o HUD e os menus
aparecem numa tela flutuante.

Feito por um apaixonado por Call of Duty. O CoD 1 foi o primeiro FPS que joguei e tenho muito carinho por ele, então
quis vivê-lo de novo em VR.

> **Aviso:** projeto não oficial de fã, sem vínculo com a Activision nem endossado por ela. Call of Duty é uma
> marca registrada da Activision Publishing, Inc. É necessária uma cópia legítima do jogo; nenhum código ou
> arquivo do jogo é incluído ou distribuído (os pacotes opcionais são gerados no seu PC a partir da sua própria cópia).

> [!WARNING]
> **Este projeto está em desenvolvimento.** Bugs e falhas visuais podem acontecer. Relate no
> [Discord](https://discord.gg/SRsK2x5Tn).

## Requisitos

- Call of Duty (2003) para PC, single player, **versão 1.5** (`CoDSP.exe`).
- Headset de PC VR com controles (testado com Quest pelo Virtual Desktop; Air Link e outros headsets de PC VR devem
  funcionar).
- Um runtime OpenXR **de 32 bits**: Virtual Desktop (VDXR) ou Oculus/Meta. O jogo é um programa de 32 bits.

## Instalação

### Mais fácil: o instalador (recomendado)

1. Baixe o `CoD-VR-Setup.exe` da [última release](https://github.com/lambda-gab/cod1-vr/releases/latest).
2. Execute. Ele acha a pasta do jogo (ou deixa você escolher) e confere se o jogo é a versão 1.5.
3. Escolha se quer esconder as mãos do personagem, deixando só a arma na sua mão (recomendado).
4. Conecte o headset e abra o `CoDSP.exe`.

Execute o instalador de novo para atualizar ou desinstalar. Nas atualizações, as suas configurações do `cod_vr.ini`
são mantidas.

### Manual

1. Extraia o `cod-vr-manual-install.zip` da [última release](https://github.com/lambda-gab/cod1-vr/releases/latest).
2. Copie `opengl32.dll`, `openxr_loader.dll` e `cod_vr.ini` para a pasta do jogo (onde está o `CoDSP.exe`).
3. Conecte o headset e abra o `CoDSP.exe`.

Para desinstalar, apague esses três arquivos. Esconder as mãos e os textos do treinamento para VR precisam do
instalador (ou dos passos nas [informações técnicas](docs/TECHNICAL.pt-BR.md)).

## Controles

Há dois modos de controle, escolhidos em `Mode` na seção `[Controllers]` do `cod_vr.ini`: **Realista** (`Realistic`, o
padrão) e **Arcade**.

### Arcade (`Mode=Arcade`)

A arma fica sempre na mão direita (`RightHanded=0` para a esquerda). Os botões são editáveis em `[Buttons]`:

| Controle | Ação |
|---|---|
| Gatilho direito | Atirar (com granada, segure para ver a trajetória e solte para lançar) |
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

Nos menus, aponte com a mão da arma: gatilho clica, B volta, A confirma.

### Realista (padrão)

As duas mãos começam vazias e você pega as armas do corpo, com qualquer uma das mãos:

| Onde você põe a mão e aperta o grip | O que você pega |
|---|---|
| No ombro direito (mão atrás da cabeça) | Arma 1 (principal) |
| No ombro esquerdo | Arma 2 (secundária) |
| No quadril, do lado daquela mão | Pistola |
| Atrás do corpo, da linha da cintura até a lombar | Granada |

- Mantenha o grip apertado para segurar a arma. Solte onde ela fica guardada (armas longas no ombro, pistola no
  quadril, granadas nas costas) para guardá-la.
- Solte em qualquer outro lugar e a arma **cai no chão**. Pegue com a mão vazia para recuperar: ela vai para
  essa mão.
- O grip da mão vazia é o "usar" (portas, pegar itens). O grip da outra mão perto da arma a segura com as duas
  mãos.
- Os dois gatilhos atiram. Com fuzil de luneta, o gatilho da outra mão dá o zoom.
- A arma na mão esquerda aparece espelhada. Os outros botões e analógicos são iguais aos do Arcade.
- A arma de cada ombro pode ser trocada em `[Realistic]` (`RightShoulder`, `LeftShoulder`), assim como as alturas
  dos coldres.

## Configurações

Tudo fica no `cod_vr.ini`, na pasta do jogo, com um comentário em cada opção. Se o jogo estiver em `Program Files`,
abra o editor (Bloco de Notas) como administrador para conseguir salvar. As mais usadas:

- `[Controllers]` `WeaponOffset*`, `WeaponPitch`, `WeaponYaw`: posição e ângulo da arma na mão.
- `[Controllers]` `TwoHandGripHeight`, `AimSmoothing`: empunhadura com duas mãos e suavização da mira.
- `[Controllers]` `MuzzleOffsetRight/Up` e `[MuzzleFlash]`: posição do clarão do disparo, também por arma
  (`thompson=frente direita cima`). São relidas durante o jogo: salve e atire de novo.
- `[VR]` `RoomScale`, `PositionalTracking`, `WorldScale`.
- `[HUD]` e `[Scope]`: tamanho e distância das telas flutuantes, suavização da luneta.
- `[Grenade]`: liga/desliga a trajetória e a opacidade dela.
- `[Controllers]` `Mode`: `Arcade` ou `Realistic`; `HipFireAccuracy`: tiros tão precisos quanto mirando (ligado por
  padrão, já que no VR você mira pela mira da própria arma).
- `[Realistic]`: soltar a arma no chão, alturas dos coldres, arma de cada ombro, tempo de sacar.

## Releases

### v1.2.0

- Novo modo de controle **Realista**, agora o padrão (`Mode=Arcade` volta aos controles anteriores): mãos vazias, armas sacadas dos ombros (arma 1 no direito,
  arma 2 no esquerdo), a pistola do quadril e as granadas das costas, com qualquer uma das mãos. Soltar a arma longe
  do coldre faz ela cair no chão, e dá para pegá-la de novo.
- Saque rápido no modo Realista: sem a animação de levantar a arma, e a arma anterior não aparece mais na troca.
- A arma na mão esquerda aparece espelhada.
- Mira melhor: os tiros vão para onde a mira aponta, sem a dispersão do tiro "do quadril" do jogo
  (`HipFireAccuracy`).
- Correção: o mundo não gira mais quando a mira passa da cabeça para a mão.
- A atualização mantém o seu `cod_vr.ini`, e as opções novas assumem o valor padrão, então quem atualiza também
  começa no modo Realista; coloque `Mode=Arcade` em `[Controllers]` para os controles anteriores. O
  `cod_vr.ini.default` traz todas as opções novas com comentários.

### v1.1.1

- Correção: os ajustes do clarão do disparo da Thompson, da M1A1 Carbine e da Springfield agora vêm nas
  configurações padrão (a v1.1.0 saiu sem eles). A atualização mantém o seu `cod_vr.ini`: copie as linhas do
  `[MuzzleFlash]` do `cod_vr.ini.default` para ele.

### v1.1.0

- Linha de trajetória da granada: mostra por onde ela vai e onde cai, parando nas paredes e passando pelas janelas.
- Os avisos na tela mostram os botões do controle, e os textos do treinamento que falam de mouse foram reescritos
  para o VR.
- O botão Menu abre o menu do jogo.
- Mira da luneta mais estável (suavização própria durante o zoom, sem flicker).
- Clarão do disparo no cano da arma (Thompson, M1A1 Carbine, Springfield), ajustável por arma.

### v1.0.0

- Primeira versão: o jogo fica jogável em VR (estéreo com rastreamento de cabeça e roomscale, arma na mão, mira pelo
  controle, HUD e menus numa tela flutuante, luneta, instalador).

## Próximos passos

- Correção de falhas visuais (artefatos).
- Suporte ao **United Offensive** (`CoDUOSP.exe`).

## Observações

- **Treinamento, as placas do início:** aponte a mão da arma para cada placa e mova a mão para cima e para baixo até o
  jogo marcar o objetivo como concluído.

## Informações técnicas

Como o mod funciona, como compilar, as notas e as ferramentas de engenharia reversa:
[docs/TECHNICAL.pt-BR.md](docs/TECHNICAL.pt-BR.md).
