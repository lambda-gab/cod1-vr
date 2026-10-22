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
- Agache de verdade e o soldado agacha. Fique em pé, reto, e clique o analógico esquerdo para recentralizar (a sua
  altura e o centro do roomscale).
- Segure a arma com as duas mãos para uma mira mais firme e menos coice.
- Os controles vibram ao chegar num coldre, ao pegar, guardar ou soltar uma arma, ao atirar e ao levar tiro.

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
- **VR:** vinheta de conforto, giro em passos ou suave, mão dominante, roomscale, agachar de verdade, menus fixos ou
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
- `[Comfort]`: a vinheta (liga/desliga, intensidade, quando começa a fechar).
- `[HUD]` `MenuFollowsHead`, `ObjectivesOnHand`, `ObjectivesWidth`, `ObjectivesBackground`: menus e a tela de
  objetivos.
- `[VR]` `MSAA`, `Anisotropy`, `CullMargin`, `HeightOffset`; `[PostFX]`: qualidade da imagem e efeitos.
- `[Controllers]` `HapticsShots`, `HapticsDamage`, `HapticsHolsters`, `PhysicalCrouch`, `CrouchDepth`,
  `TwoHandStabilization`, `TwoHandRecoilReduction`.

## Releases

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
