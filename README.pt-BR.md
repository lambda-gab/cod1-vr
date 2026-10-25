![Call of Duty VR](docs/images/banner.jpg)

# CALL OF DUTY (2003) VR

[English](README.md) | **Português**

Mod de PC VR para a **campanha single player do Call of Duty 1** (`CoDSP.exe` 1.3) e a sua expansão **United Offensive** (`CoDUOSP.exe` 1.51). Jogue o original de 2003 no
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

### Apoiadores no Patreon

<p align="center">
  <img src="docs/images/supporters/master.png" width="30" alt="Master"> <b>Dmitriy Melnikov</b><br>
  <img src="docs/images/supporters/master.png" width="30" alt="Master"> <sub>Master</sub>
</p>

## Requisitos

- Call of Duty (2003) para PC, single player, **versão 1.3** (`CoDSP.exe`), e/ou Call of Duty: United Offensive,
  single player, **versão 1.51** (`CoDUOSP.exe`).
- Headset de PC VR com controles (testado com Quest pelo Virtual Desktop; Air Link e outros headsets de PC VR devem
  funcionar).
- Um runtime OpenXR **de 32 bits**: Virtual Desktop (VDXR), Oculus/Meta ou SteamVR (o runtime OpenXR de 32 bits
  dela vem no **beta da SteamVR**: Steam > SteamVR > Propriedades > Betas). O jogo é um programa de 32 bits.

> [!NOTE]
> **Testado com o Quest 3, onde funciona perfeitamente.** Se você tiver algum problema com outro headset,
> [crie uma issue](https://github.com/lambda-gab/cod1-vr/issues) para eu analisar.

## Instalação

> [!IMPORTANT]
> **O VR não inicia, ou o jogo abre como duas imagens planas lado a lado?** Leia o
> **[guia de solução de problemas](TROUBLESHOOTING.pt-BR.md)**. Ele também explica como salvar um **relatório de
> diagnóstico** (launcher > ABOUT > "Problem starting in VR?") para me enviar.

### Mais fácil: o instalador (recomendado)

1. Baixe o `CoD-VR-Setup.exe` da [última release](https://github.com/lambda-gab/cod1-vr/releases/latest).
2. Execute. Ele acha a pasta do jogo (ou deixa você escolher) e confere se o jogo é a versão 1.3 (United Offensive: 1.51).
3. Escolha se quer esconder as mãos do personagem, deixando só a arma na sua mão (recomendado), e se quer criar um
   atalho do launcher na área de trabalho.
4. Conecte o headset e abra o jogo pelo **launcher do Call of Duty VR** (o atalho, ou o `CoD-VR-Launcher.exe` na
   pasta do jogo).

Execute o instalador de novo para atualizar ou desinstalar. Nas atualizações, as suas configurações são mantidas.

### Manual

1. Extraia o `cod-vr-manual-install.zip` da [última release](https://github.com/lambda-gab/cod1-vr/releases/latest).
2. Copie `opengl32.dll`, `openxr_loader.dll`, `cod_vr.ini`, `supporters.ini` e `CoD-VR-Launcher.exe` para a pasta do jogo (onde está
   o `CoDSP.exe` ou o `CoDUOSP.exe`).
3. Abra o `CoD-VR-Launcher.exe`, vá na aba **VR** e mude **Player hands** para **Hidden** (os braços do soldado
   aparecem até você fazer isso; o launcher gera o arquivo a partir do seu próprio jogo, e o Windows pode pedir
   direitos de administrador).
4. Conecte o headset e aperte PLAY (ou abra direto o `CoDSP.exe`).

Para desinstalar, apague esses cinco arquivos (e o `main\pakz_vr_nohands.pk3`, se você escondeu as mãos). Os textos
do treinamento para VR precisam do instalador (ou dos passos nas [informações técnicas](docs/TECHNICAL.pt-BR.md)).

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
- Agache de verdade e o soldado agacha. Fique em pé, reto, e clique o analógico esquerdo para recentralizar (a sua
  altura e o centro do roomscale).
- Segure a arma com as duas mãos para uma mira mais firme e menos coice.
- Os controles vibram ao chegar num coldre, ao pegar, guardar ou soltar uma arma, ao atirar e ao levar tiro.

Botões (editáveis em `[Buttons]` no `cod_vr.ini`):

| Controle | Ação |
|---|---|
| Qualquer gatilho | Atirar (com granada: segure para ver a trajetória e solte para lançar). Com fuzil de luneta, o gatilho da mão que não segura a arma dá o zoom |
| Grip da mão vazia, ou da mão livre longe da arma | Usar / pegar (portas, armas no chão). Plantar explosivos: com uma arma numa mão, leve a outra até a bomba e segure o grip dela |
| Grip da mão livre no guarda-mão da arma | Segurar a arma com as duas mãos |
| Analógico esquerdo | Andar (relativo à cabeça). Com Blink ou Shift (beta): empurre para a frente para mirar um salto, solte para ir |
| Analógico direito | Girar (snap 45°); cima/baixo muda a postura |
| A / B (os botões da mão da arma) | Pular / recarregar (canhoto: X / Y) |
| X, Y (os botões da outra mão; canhoto: A, B) | Coringas: um toque executa a ação do próprio botão (nenhuma, por padrão), segurar um e apertar outro botão executa um atalho (trocar de arma em Y + A). Editáveis no launcher |
| Costas da mão sem arma, viradas para os olhos | Objetivos, como olhar um relógio. Coronhada: balance a arma |
| Clique analógico esquerdo | Recentralizar (fique em pé, reto) |
| Clique analógico direito | Deitar |
| Menu | Menu do jogo |

Nos menus, aponte com a mão direita: gatilho clica, B volta, A confirma.

## Configurações

### O launcher

O launcher tem as opções que a maioria quer mexer, salvas assim que você muda:

- **GRAPHICS:** resolução no headset (de *Potato* 50% a *GPU Furnace* 200%, com o tamanho por olho), antialiasing
  (MSAA), filtro de texturas (anisotrópico), qualidade das texturas, brilho, e opções de teste de pós-processamento
  (oclusão de ambiente, nitidez, estilo de cor, bloom: deixe desligadas se a imagem piorar).
- **VR:** movimento (contínuo, ou saltos Blink / Shift, beta), vinheta de conforto, giro em passos ou suave, roomscale, agachar de verdade, menus fixos ou
  seguindo a cabeça, o que do HUD fica na tela, trajetória da granada, estilo da mira, golpe balançando a arma,
  soltar a arma no chão, firmeza e coice com as duas mãos, objetivos na mão e intensidade da vibração para tiros,
  dano e coldres.
- **MODS:** coisas que o jogo original não tem: dificuldade personalizada (*Real life*: um tiro mata, você e eles;
  *Perma death*: morreu, acabou a campanha; as duas; *God mode*), munição infinita e sangue (desligado, ligado ou
  *Gore*).
- **CONTROLS:** o mapa dos controles.

Com a versão Steam do jogo, o PLAY abre o jogo pela Steam (o `CoDSP.exe` dela só roda quando a Steam o abre).

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
- `[Comfort]`: a vinheta (liga/desliga, intensidade, quando começa a fechar), `VehicleTurnSmooth`, `ShakeKeep`.
- `[HUD]` `MenuFollowsHead`, `ObjectivesOnHand`, `ObjectivesWidth`, `ObjectivesBackground`: menus e a tela de
  objetivos.
- `[VR]` `MSAA`, `Anisotropy`, `CullMargin`, `HeightOffset`; `[PostFX]`: qualidade da imagem e efeitos.
- `[Controllers]` `HapticsShots`, `HapticsDamage`, `HapticsHolsters`, `PhysicalCrouch`, `CrouchDepth`,
  `TwoHandStabilization`, `TwoHandRecoilReduction`.

## Releases

### v1.7.0

- **Call of Duty: United Offensive** (`CoDUOSP.exe` 1.51) funciona, campanha single player. O instalador e o launcher
  o aceitam (a página inicial do launcher tem um cartão para cada jogo que você tem), e tudo abaixo vale para os
  dois jogos. Extras do United Offensive: a metralhadora do jipe é mirada com o controle; as metralhadoras de bipe
  (.30 cal, MG34, DP28) são armadas com o gatilho da mão livre (aperte de novo para desarmar); a escada da Foy, o
  início de Noville no tanque e as torretas do bombardeiro funcionam; o Perma death apaga os saves do próprio United
  Offensive.
- **As torretas do bombardeiro** (dorsal e de cauda): você fica no assento da arma enquanto ela gira, o controle que você
  aponta a mira (a cabeça olha livremente), o assento é o mesmo onde quer que o controle aponte quando você sobe, o
  botão de recentralizar traz você de volta a ele, apertar um grip faz você descer e a mira simples some nas torretas.
- **Movimento** (aba VR): *Continuous* anda com o analógico, como antes (o padrão). **Blink (beta)** e **Shift (beta)** se
  movem por saltos: empurre o analógico de movimento para a frente para mirar um arco com a mão daquele analógico (a
  mão da arma fica livre) e solte para ir até lá. O destino precisa ser chão onde você cabe. O Blink escurece e
  clareia a tela; o Shift desliza.
- **Botões.** Os dois botões da mão que não segura a arma (X e Y, ou A e B se você é canhoto) são coringas: um toque
  executa a ação do próprio botão, segurar um e apertar outro botão executa um atalho (trocar de arma em Y + A, por
  padrão). Os dois botões da mão da arma agem na hora: A = Pular, B = Recarregar (canhoto: X = Pular, Y = Recarregar),
  para recarregar com o polegar no analógico de movimento.
- **Tela CONTROLS:** a mão dominante é a primeira escolha e os padrões seguem ela (escolha Left e o "Default" é o layout de
  canhoto, então o perfil Leftie separado acabou). A tela não tem mais textos longos, e o launcher ficou um pouco mais alto,
  com a página inicial centralizada.
- **Plantar explosivos e usar coisas com a mão livre enquanto a outra segura uma arma:** leve a mão livre até a bomba (ou
  interruptor, porta) e segure o grip. Antes era preciso guardar a arma.
- **Objetivos** aparecem nas costas da mão que não segura a arma, como um relógio: vire para os olhos. A tela ficou maior
  e o fundo mais opaco (a caixa das legendas também).
- **Patreon Supporters:** quem apoia o projeto no Patreon aparece num painel ao lado do menu principal do jogo, no
  headset (a lista é o `supporters.ini` na pasta do jogo; o launcher desenha o painel).
- **Coldres:** chegar a um coldre que você pode usar dá uma vibração de um segundo, não mais contínua.
- **Lunetas:** uma máscara redonda fecha a visão durante o zoom.
- **Aba VR:** novas opções *Vehicle turns* e *Camera shake* (como a visão segue as curvas de um veículo, quanto ela
  treme em explosões).
- **Correções:** a dica "incline-se para fora da janela" só no passeio de carro; a cintilação das árvores no United
  Offensive (o cache de modelos estáticos); Pimax: o VR inicia quando o runtime registrou o arquivo de 64 bits para
  programas de 32 bits.
### v1.6.9

- **As missões do barco** ("ship", a travessia para Stalingrad, e o começo de "Stalingrad", que começa num barco): sem
  vinheta, sem "segure os dois gatilhos para pular a missão" e sem as mensagens de passeio. As missões exigem que você
  se agache, então o stick livre para cima e para baixo voltam a ser os botões de postura ali. A cabeça livre e a
  visão seguindo o barco continuam como estavam. Os carros e caminhões (carride, truckride, ...) mantêm tudo.
- **A versão do jogo é a 1.3, não a 1.5:** a 1.3 é o último patch single player do Call of Duty (a 1.5 é a do
  multiplayer). O README, o guia de problemas, as notas técnicas, as mensagens do instalador e o log diziam 1.5. O mod
  em si não mudou: ele reconhece o mesmo `CoDSP.exe`.

### v1.6.8

- **Esconder os braços do personagem pelo launcher** (aba VR > **Player hands**: Shown / Hidden). Na instalação
  manual não há instalador para criar o arquivo "sem mãos", então os braços ficavam visíveis; agora o launcher cria
  (ou remove) o arquivo a partir dos arquivos do seu próprio jogo. O Windows pode pedir direitos de administrador. Os
  passos da instalação manual deste README mandam fazer isso.
- A última linha da coluna esquerda da aba VR (Subtitle size) era desenhada embaixo da linha de dica no rodapé: as
  linhas ficaram mais próximas.

### v1.6.7

- **Crash na missão "rocket"** ao lutar com os inimigos no começo: o desenho dos modelos estáticos do jogo (objetos,
  árvores, entulho) enche uma lista de tamanho fixo e escrevia além do fim dela quando havia muitos à vista. Agora o
  mod deixa de fora, naquele quadro, os modelos que não cabem, em vez de deixar o jogo travar (uma linha no log,
  `static models: the index cache is full`, avisa quando acontece).

### v1.6.6

- **A arma balançava na mão como um elástico quando você andava ou se movia de lado** (v1.6.4 e v1.6.5): a redução do
  tremor de câmera mudava o ponto de onde a arma é posicionada. Agora a arma fica firme na mão.
- **Sem o nome ou a imagem da arma quando você chegava perto de uma** com o HUD em "Imersivo", "Só legendas" ou "Só
  bússola" (v1.6.5): as dicas de uso do jogo aparecem de novo em todos os modos de HUD.
- **Menus e telas de carregamento não seguem mais a cabeça** nos primeiros segundos (dava tontura): aparecem na sua
  frente e ficam ali; só se movem de novo se a cabeça ficar bem acima ou abaixo (você sentou ou levantou) ou ao
  recentralizar.
- **A visão não entra mais em paredes e batentes de porta** quando você se inclina ou anda para eles com roomscale:
  ela para um pouco antes da parede. `[VR]` `HeadCollision=0` desliga. Portas ainda não são detectadas.
- **Armas fixas (a MG42, os rifles antitanque PTRS41) não empurram mais você** ao mirar para baixo: a arma deixa de ser sólida
  para você enquanto está nela. `[Turret]` `NoCollision`, e `OffsetX/Y/Z` (cm) para mudar onde você fica.
- **Pegar a arma largada:** com só um rifle e uma granada, o rifle largado pode ser pego de novo.
- **Tamanho das legendas** na aba VR do launcher (Pequeno, Médio, Grande, Extra Grande; `[HUD]` `SubtitleSize`).
- **Runtime OpenXR:** um runtime de 64 bits em `XR_RUNTIME_JSON` (SteamVR) é ignorado por este jogo de 32 bits, e o
  log de início tem mais etapas para achar falhas no runtime do headset.
- O relatório de diagnóstico (SOBRE) agora também mostra se o `main\pakz_vr_nohands.pk3` (as mãos escondidas) está instalado.

### v1.6.5

- **A imagem tremia quando você virava a cabeça** (v1.6.4): a redução do tremor de câmera também suavizava o giro da
  visão do jogo, que carrega os seus próprios giros de cabeça, e a imagem ficava para trás e balançava alguns graus
  enquanto você girava. Agora ela só reduz o tremor da posição da câmera (terremotos, bombardeios) e não mexe no
  giro.

### v1.6.4

- **Vibração ao atirar com armas fixas e com a Panzerfaust:** a MG42, a Flak 88, a Flak Vierling e o tanque agora
  fazem os dois controles vibrarem a cada tiro (o canhão mais forte e mais longo); a Panzerfaust dá um empurrão forte
  nas duas mãos. A linha da granada não aparece mais enquanto você está numa arma fixa.
- **Truckride, a Panzerfaust que o Waters entrega:** dentro de um veículo, o grip ou o gatilho de uma mão vazia agora
  pega a arma que é estendida para você.
- **Curvas de veículo mais suaves:** a visão acompanha o caminhão ou o carro numa curva mais devagar (e a no máximo 50
  graus por segundo), então uma curva fechada não gira mais toda a visão de uma vez. `[Comfort]` `VehicleTurnSmooth`
  (segundos; 0,15 era o comportamento antigo, 0,5 é o novo padrão).
- **Briefings e slides** (as imagens entre as missões, como as da campanha russa): aparecem numa tela à sua frente,
  como os menus, em vez de uma cópia em cada olho (que ficava dobrada e desalinhada), e o gatilho pula ("fire to
  skip").
- **Stalingrad, a fila de munição:** a caminhada roteirizada pela fila não vira mais você de costas (era tratada como
  um banco de veículo), e a cabeça fica livre durante ela.
- **Menos tremor de câmera** em terremotos e explosões (o bombardeio do fim de Stalingrad): o tremor é reduzido a 40%.
  `[Comfort]` `ShakeKeep` (1 = como o jogo balança, 0 = nenhum).

### v1.6.3

- **O VR não iniciava quando o Windows não tem um runtime OpenXR ativo** (SteamVR, Meta ou outro software de VR
  instalado, mas nenhum escolhido como ativo): o jogo não achava o headset. O mod agora escolhe o runtime do software
  de VR que está rodando (SteamVR, depois Virtual Desktop, depois Meta/Oculus), ou o único instalado.
- **Relatório de diagnóstico:** avisa claramente quando nenhum runtime está definido como ativo e como escolher um.

### v1.6.2

- **O VR não iniciava para alguns jogadores** (o jogo abria como duas imagens planas lado a lado): outros programas
  registram "API layers" do OpenXR (overlays e auxiliares de outros jogos de VR) que são carregadas em todo programa
  OpenXR, e uma com arquivo ausente ou errado impedia o OpenXR de iniciar (`XR_ERROR_FILE_ACCESS_ERROR`). O mod agora
  desliga essas layers para o jogo e tenta de novo.
- **Relatório de diagnóstico** (launcher > ABOUT): lista as API layers e testa a mesma correção, não apaga mais o
  último log do jogo, lista corretamente os programas de VR em execução e avisa quando a SteamVR não tem o runtime
  OpenXR de 32 bits (vem com o beta da SteamVR) ou não é a que o jogo usa.

### v1.6.1

- **Launcher:** corrigido: as outras abas não respondiam depois de abrir a aba VR.
- **Sticks** agora diz o que faz: "Left: move, Right: turn" ou "Left: turn, Right: move", seja qual for a mão
  da arma (substitui o "Swapped"). O padrão é o analógico esquerdo andando.

### v1.6.0

- **Veículos** (carro, caminhão, Flak 88, tanque, o carro de Dawnville): a cabeça fica livre dentro deles, a visão
  acompanha o veículo sem os solavancos e os tiros vão para onde você mira, então dá para acertar inimigos do
  carro e do caminhão. Intensidade de vinheta própria para veículos (launcher) e um aviso para pular a missão se
  você passar mal (segure os dois gatilhos por 4 segundos). A Flak 88 é mirada com a cabeça e se segura ou solta
  com as empunhaduras.
- **Tanque:** o analógico esquerdo dirige, de forma suave (pesado, demora para arrancar e parar); o canhão segue
  para onde sua cabeça olha.
- **Metralhadoras MG42:** as duas empunhaduras para montar (as mãos seguram e miram), solte uma para descer.
- **Escadas:** empurre o analógico contra a escada e aponte a mão da arma para cima para subir. As placas do
  treinamento contam só de olhar para elas.
- **Fonte moderna** (aba GRAPHICS): o texto do jogo em Segoe UI, gerada pelo instalador a partir da do Windows.
- **Controles:** "Sticks: Swapped" no launcher (andar com o analógico da mão da arma, para canhotos que
  continuam andando pela esquerda), e as dicas do jogo mostram os botões do seu controle.
- **Sem tremor de tela** em explosões e shell shock (gerado pelo instalador), a Panzerfaust sempre mostra mira e a
  arma não atravessa mais o chão ao deitar.
- **Launcher:** avisa quando há uma versão nova, e o ABOUT tem **"Problem starting in VR?"**: salva na Área de
  Trabalho um relatório de diagnóstico (os runtimes OpenXR registrados para programas de 32 bits, o que o loader
  do jogo informa, o software do seu headset, as configurações e o log do jogo) para mandar no Discord.

### v1.5.0

- **SteamVR e outros runtimes:** a imagem no headset ficava esticada e com zoom (na tela do PC estava certa), e
  preta em resoluções baixas, com o runtime de 32 bits da SteamVR e outros. Agora os olhos são entregues no tamanho
  que o headset pede, seja qual for a resolução escolhida.
- **Versão Steam:** o PLAY abre o jogo pela Steam, que o `CoDSP.exe` dela exige.
- **Golpe corpo a corpo balançando a arma:** acerte com qualquer parte dela, da coronha ao cano. Só conta um golpe de
  verdade (a velocidade se ajusta no launcher), não encostar a arma em alguém.
- **Modos de HUD:** completo, imersivo (nada), só legendas, só bússola ou só vida e munição. Segurar o botão de
  Objetivos mostra tudo.
- **Mira** (desligada por padrão), em seis estilos mostrados no launcher: cruz, ponto, círculo, círculo + ponto,
  cruz + ponto.
- **Andar e correr** seguem a inclinação do analógico, e não ficam mais lentos quando a arma aponta para outro lado.
- **Aba MODS:** dificuldade personalizada (Real life, Perma death, as duas, God mode), munição infinita e sangue
  (desligado, ligado ou Gore: mais sangue que espirra e cai, gerado pelo instalador a partir do efeito do seu jogo).
- As linhas sobre o mundo (apontador dos menus, trajetória da granada, mira) estão mais nítidas.

### v1.4.0

- **Vibração:** ao chegar com a mão num coldre, ao pegar, guardar ou soltar uma arma, a cada tiro (fuzis de luneta
  também) e ao levar tiro. Cada tipo tem a sua intensidade no launcher.
- **Agachar de verdade:** agache e o soldado agacha, com a visão na sua altura real.
- **Recentralizar** com o clique do analógico esquerdo: a sua altura e o centro do roomscale.
- **Firmeza com as duas mãos:** segurar a arma com as duas mãos amortece o tremor e reduz o coice.
- **Qualidade de imagem:** antialiasing (MSAA), filtro anisotrópico, texturas em 32 bits e opções de teste de
  pós-processamento (oclusão de ambiente, nitidez, estilos de cor, bloom).
- Os objetivos na mão ganharam um fundo escuro de cantos arredondados, mais fácil de ler.
- O launcher fica aberto enquanto você joga, para testar configurações; a página VR está em duas colunas.
- **Correção:** soldados desenhados com peças de outros modelos (equipamento, cabeças) no olho esquerdo.
- **Correção:** texturas piscando nas bordas da visão ao virar a cabeça rápido.

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

## Informações técnicas

Como o mod funciona, como compilar, as notas e as ferramentas de engenharia reversa:
[docs/TECHNICAL.pt-BR.md](docs/TECHNICAL.pt-BR.md).
