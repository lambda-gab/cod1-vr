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

## Apoie o desenvolvedor

> [!TIP]
> **Gostaria de ajudar o DEV? Com $2 você mantém o Claude Pro.**
>
> Todo jogo portado é e sempre será de graça para todos. Sua contribuição ajuda a pagar o plano de IA e me faz gastar
> algumas horas do dia para trazer alegria para os gamers.
>
> [**Apoiar no Patreon**](https://www.patreon.com/cw/LambdaGab)

## Requisitos

- Call of Duty (2003) para PC, single player, **versão 1.5** (`CoDSP.exe`).
- Headset de PC VR com controles (testado com Quest pelo Virtual Desktop; Air Link e outros headsets de PC VR devem
  funcionar).
- Um runtime OpenXR **de 32 bits**: Virtual Desktop (VDXR) ou Oculus/Meta. O jogo é um programa de 32 bits.

## Instalação

### Mais fácil: o instalador (recomendado)

1. Baixe o `CoD-VR-Setup.exe` da [última release](https://github.com/lambda-gab/cod1-vr/releases/latest).
2. Execute. Ele acha a pasta do jogo (ou deixa você escolher) e confere se o jogo é a versão 1.5.
3. Escolha se quer esconder as mãos do personagem, deixando só a arma na sua mão (recomendado), e se quer criar um
   atalho do launcher na área de trabalho.
4. Conecte o headset e abra o jogo pelo **launcher do Call of Duty VR** (o atalho, ou o `CoD-VR-Launcher.exe` na
   pasta do jogo).

Execute o instalador de novo para atualizar ou desinstalar. Nas atualizações, as suas configurações são mantidas.

### Manual

1. Extraia o `cod-vr-manual-install.zip` da [última release](https://github.com/lambda-gab/cod1-vr/releases/latest).
2. Copie `opengl32.dll`, `openxr_loader.dll`, `cod_vr.ini` e `CoD-VR-Launcher.exe` para a pasta do jogo (onde está
   o `CoDSP.exe`).
3. Conecte o headset e abra o `CoD-VR-Launcher.exe` (ou direto o `CoDSP.exe`).

Para desinstalar, apague esses quatro arquivos. Esconder as mãos e os textos do treinamento para VR precisam do
instalador (ou dos passos nas [informações técnicas](docs/TECHNICAL.pt-BR.md)).

## Controles

As armas são pegas do corpo, com qualquer uma das mãos, e ficam na mão enquanto o grip está apertado:

| Ponha a mão aqui e aperte o grip | Você pega |
|---|---|
| No ombro direito (mão atrás da cabeça) | Arma 1 (principal) |
| No ombro esquerdo | Arma 2 (secundária) |
| No quadril, do lado daquela mão | Pistola |
| Atrás do corpo, da linha da cintura até a lombar | Granada |

- Solte o grip onde a arma fica guardada (armas longas no ombro, pistola no quadril, granadas nas costas) para
  guardá-la.
- Solte em qualquer outro lugar e a arma **cai no chão**. Pegue com a mão vazia para recuperar: ela vai para
  essa mão.
- A arma na mão esquerda aparece espelhada.

Botões (editáveis em `[Buttons]` no `cod_vr.ini`):

| Controle | Ação |
|---|---|
| Qualquer gatilho | Atirar (com granada: segure para ver a trajetória e solte para lançar). Com fuzil de luneta, o gatilho da mão que não segura a arma dá o zoom |
| Grip da mão vazia | Usar / pegar (portas, armas no chão) |
| Grip da outra mão, perto da arma | Segurar a arma com as duas mãos |
| Analógico esquerdo | Andar (relativo à cabeça) |
| Analógico direito | Girar (snap 45°); cima/baixo muda a postura |
| A / B | Pular / recarregar |
| X | Objetivos (aparecem acima da mão esquerda) |
| Y | Coronhada |
| Clique analógico esquerdo | Objetivos |
| Clique analógico direito | Deitar |
| Menu | Menu do jogo |

Nos menus, aponte com a mão direita: gatilho clica, B volta, A confirma.

## Configurações

### O launcher

O launcher tem as opções que a maioria quer mexer, salvas assim que você muda:

- **GRAPHICS:** resolução no headset (de *Potato* 50% a *GPU Furnace* 200%, com o tamanho por olho), qualidade das
  texturas, brilho.
- **VR:** vinheta de conforto, giro em passos ou suave, mão dominante, roomscale, trajetória da granada, soltar a arma
  no chão, objetivos na mão, menus fixos ou seguindo a cabeça.
- **CONTROLS:** o mapa dos controles.

As escolhas vão para `%APPDATA%\CoD-VR\settings.ini` e têm prioridade sobre o `cod_vr.ini`.

### cod_vr.ini

O resto fica no `cod_vr.ini`, na pasta do jogo, com um comentário em cada opção. Se o jogo estiver em `Program Files`,
abra o editor (Bloco de Notas) como administrador para conseguir salvar. As mais usadas:

- `[Controllers]` `WeaponOffset*`, `WeaponPitch`, `WeaponYaw`: posição e ângulo da arma na mão.
- `[Controllers]` `TwoHandGripHeight`, `AimSmoothing`: empunhadura com duas mãos e suavização da mira.
- `[Controllers]` `MuzzleOffsetRight/Up` e `[MuzzleFlash]`: posição do clarão do disparo, também por arma
  (`thompson=frente direita cima`). São relidas durante o jogo: salve e atire de novo.
- `[VR]` `RoomScale`, `PositionalTracking`, `WorldScale`.
- `[HUD]` e `[Scope]`: tamanho e distância das telas flutuantes, suavização da luneta.
- `[Grenade]`: liga/desliga a trajetória e a opacidade dela.
- `[Controllers]` `HipFireAccuracy`: tiros tão precisos quanto mirando (ligado por padrão, já que no VR você mira
  pela mira da própria arma); `RightHanded`: qual analógico gira.
- `[Holsters]`: arma de cada ombro (canhotos podem querer trocar), alturas dos coldres, soltar a arma no chão, tempo
  de sacar.
- `[VR]` `RenderScale`: resolução de cada olho, vezes a recomendada pelo headset.
- `[Comfort]`: a vinheta (liga/desliga, intensidade, quando começa a fechar).
- `[HUD]` `MenuFollowsHead`, `ObjectivesOnHand`, `ObjectivesWidth`: menus e a tela de objetivos.

## Releases

### v1.3.0

- **Launcher** com PLAY, CONTROLS, GRAPHICS, VR e ABOUT; o instalador pode criar um atalho dele na área de trabalho.
- **Imagem mais nítida:** cada olho é renderizado na resolução do próprio headset (antes era a da janela do jogo,
  esticada), ajustável de 50% a 200%.
- **Vinheta de conforto** ao andar com o analógico ou girar suave (Off / Low / Medium / High).
- Menus e telas de carregamento ficam fixos à sua frente, dá para olhar em volta deles.
- O X abre os objetivos, que aparecem acima da mão esquerda.
- Treinamento: com as mãos vazias, uma linha mostra para onde a mão direita aponta, até as placas serem concluídas.
- Os controles agora são sempre os de coldres no corpo (o modo Arcade foi removido).

Obrigado ao [lasmith1689-sys/cod1-vr](https://github.com/lasmith1689-sys/cod1-vr) pelas ideias da vinheta e da
resolução do headset.

### v1.2.0

- As armas são pegas do corpo: dos ombros (arma 1 no direito, arma 2 no esquerdo), do quadril (pistola) e das costas
  (granadas), com qualquer uma das mãos. Soltar a arma longe do coldre faz ela cair no chão, e dá para pegá-la de
  novo.
- Saque rápido: sem a animação de levantar a arma, e a arma anterior não aparece mais na troca.
- A arma na mão esquerda aparece espelhada.
- Mira melhor: os tiros vão para onde a mira aponta, sem a dispersão do tiro "do quadril" do jogo
  (`HipFireAccuracy`).
- Correção: o mundo não gira mais quando a mira passa da cabeça para a mão.

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

- **Treinamento, as placas do início:** aponte a mão direita (uma linha mostra para onde) para cada placa e mova a
  mão para cima e para baixo até o jogo marcar o objetivo como concluído.

## Informações técnicas

Como o mod funciona, como compilar, as notas e as ferramentas de engenharia reversa:
[docs/TECHNICAL.pt-BR.md](docs/TECHNICAL.pt-BR.md).
