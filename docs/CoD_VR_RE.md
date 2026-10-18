# Call of Duty (2003) SP → VR: engenharia reversa do CoDSP.exe

Ferramentas: REA 6.5.0 (`rea-agents`) com o provider Ghidra 12.1.4 (JDK 21).
Alvo: `CoDSP.exe` 1.5 (02/03/2004), base `0x400000`, x86, sem ASLR. Endereços em `codsp_addresses.h`.

## Conclusão principal

O CoDSP.exe ainda tem **o caminho de estéreo do Quake 3 / RTCW intacto**:

1. `SCR_UpdateScreen` (`0x410590`): se `cls.glconfig.stereoEnabled` (`0x01439440`) for diferente de 0,
   chama `SCR_DrawScreenField` duas vezes, com `ESI=1` (STEREO_LEFT) e depois `ESI=2` (STEREO_RIGHT).
2. `SCR_DrawScreenField` (`0x410410`) faz `push esi; call re.BeginFrame` → `RE_BeginFrame(stereoFrame)`.
3. `RE_BeginFrame` (`0x4d4680`) valida o estéreo e emite o comando `glDrawBuffer(GL_BACK_LEFT/RIGHT)`.
4. O GLimp (`0x4fd300`) só adiciona `PFD_STEREO` ao pixel format quando o estéreo é pedido.

Portanto **não é preciso reescrever o renderer**: basta forçar as duas flags de estéreo e capturar
`glDrawBuffer(GL_BACK_LEFT/RIGHT)` para desenhar em texturas por olho (sem pixel format quad-buffer).

## Pontos de hook

| O quê | Onde | Uso no VR |
|---|---|---|
| `re` do cliente (tabela de ponteiros, 0xec bytes) | `0x01426d60` | Hook sem patch de código: trocar ponteiros |
| `re.RenderScene` → `RE_RenderScene` | `+0x64` (trap cgame `0x47`), `0x4deec0` | Por olho: deslocar `vieworg` em ±IPD/2, rodar `viewaxis` pela pose do olho, trocar fov |
| `re.BeginFrame(stereoFrame)` | `+0x88`, `0x4d4680` | Saber qual olho está sendo desenhado; pegar a pose do OpenXR no início do quadro |
| `re.EndFrame` | `+0x8c`, `0x4d4a60` | Fim do quadro |
| `GLimp_EndFrame` → `gdi32!SwapBuffers` | `0x4ffb40` | Hook na IAT: `xrEndFrame` (enviar as duas texturas) |
| `R_RenderView(viewParms)` | `0x4cb270` | Projeção assimétrica por olho (como o `tr_main.c` do RTCWQuest) |
| `cl.viewangles[3]` | `0x0142f304` | Yaw/pitch do headset (como o `cl_input.c` do RTCWQuest) |
| `CL_AdjustAngles` / `CL_MouseMove` | `0x40a5b0` / `0x40aa10` | Desligar o pitch do mouse; giro (snap/smooth) pelo analógico |
| `CL_CgameSystemCalls` | `0x401a50` | Interceptar syscalls do cgame (ClearScene `0x3d`, AddRefEntity `0x3e`, RenderScene `0x47`) |

O `refdef_t` lido pelo `RE_RenderScene` tem o mesmo layout do RTCW: `x,y,w,h, fov_x, fov_y, vieworg[3],
viewaxis[3][3], time, rdflags`. Cenas com `rdflags & 1` (RDF_NOWORLDMODEL) são HUD/menus/modelos 2D:
não devem receber a pose do headset.

O `opengl32` é carregado dinamicamente (`LoadLibrary("opengl32")` + `wglGetProcAddress` / QGL), então
um **proxy `opengl32.dll`** na pasta do jogo é carregado antes do DLL do sistema.

## Diferença em relação ao RTCWQuest

O RTCWQuest altera o **código-fonte** (GPL) do RTCW: `cg_view.c`, `tr_main.c`, `cl_input.c`,
`cg_weapons.c` e `android_glimp.c`. O CoD não tem código aberto, então a mesma lógica precisa ser
injetada em tempo de execução: os hooks acima são os equivalentes binários desses arquivos.
`VrClientInfo.h` (estado do HMD/controles) e a matemática de pose podem ser reaproveitados quase sem mudanças.

## Arquitetura proposta (PC VR, OpenXR, x86 32 bits)

```
CoDSP.exe
 └─ opengl32.dll (proxy, nosso)  → repassa ao opengl32 do sistema
     ├─ ao carregar: hook em ADDR_re_client (+0x64 / +0x88), IAT de SwapBuffers
     ├─ força stereoEnabled em 0x015347d8 e 0x01439440
     ├─ glDrawBuffer(GL_BACK_LEFT/RIGHT) → bind do FBO do olho (swapchain OpenXR)
     ├─ RenderScene hook → pose do olho no vieworg/viewaxis + fov assimétrico
     ├─ CL_AdjustAngles/MouseMove → orientação do HMD em cl.viewangles
     └─ SwapBuffers hook → xrEndFrame
```

O `openxr_loader` é 64 bits por padrão; para um processo de 32 bits é preciso compilar o loader
OpenXR para Win32 (o runtime da Oculus/SteamVR aceita clientes de 32 bits).

## Fases

1. **Estéreo no monitor**: proxy opengl32 + estéreo forçado, lado a lado na janela (valida os hooks sem headset).
2. **Headset**: OpenXR (sessão, swapchains, poses) + rastreamento de cabeça (3DoF/6DoF).
3. **HUD/menus** numa camada quad do OpenXR (como o `using_screen_layer` do RTCWQuest).
4. **Controles**: mira pela mão (`CG_CalcMuzzlePoint` e o view model, no cgamex86.dll) e bindings.

## Fase 1: proxy opengl32.dll (estéreo lado a lado)

Código em `proxy/` (`cod_vr.c`, `stubs.c` gerado por `gen_stubs.py`, `opengl32.def`, `build.bat`).
Saída: `proxy/out/opengl32.dll` + `proxy/out/cod_vr.ini`.

- Encaminha os 368 exports do `SysWOW64\opengl32.dll`; só `glDrawBuffer`, `glReadBuffer` e `wglMakeCurrent` são próprios.
- Hooks: `re.BeginFrame` e `re.RenderScene` (tabela `0x01426d60`) e a IAT de `SwapBuffers` (`0x53302c`).
  Só são instalados se o `TimeDateStamp` do exe for `0x40443fda` (CoDSP.exe 1.5); em qualquer outro exe a DLL só encaminha as chamadas.
- `BeginFrame(0)` desliga o estéreo do renderer só durante a chamada, o que evita o erro fatal do `RE_BeginFrame`
  na tela de carregamento (`0x410937`). Os dois flags de estéreo ficam ligados a partir do quadro seguinte.
- `glDrawBuffer(GL_BACK_LEFT/RIGHT)` → FBO por olho (RGBA8 + depth24/stencil8, do tamanho de `vidWidth`×`vidHeight`).
- `RenderScene`: desloca `vieworg` em ±`Separation`/2 ao longo de `viewaxis[1]`; ignora cenas com `RDF_NOWORLDMODEL`.
- `SwapBuffers`: desenha os dois olhos lado a lado (meia largura cada, "half SBS") e faz o swap.
- Log em `cod_vr.log` na pasta do jogo. Como o CoDSP.exe não tem manifest, o Windows redireciona a escrita para
  `%LOCALAPPDATA%\VirtualStore\<pasta do jogo sem a letra do drive>\cod_vr.log`.
  O mesmo vale para o `cod_vr.ini`: se uma cópia editada existir na VirtualStore, é ela que o jogo lê.

Instalação: copiar `opengl32.dll` e `cod_vr.ini` para a pasta do CoDSP.exe. Para desinstalar, basta apagar os dois arquivos.

Testado: compilação x86 sem warnings, exports idênticos aos do sistema, e carga + contexto GL + chamadas
encaminhadas num exe de teste (`proxy/test/gltest.c`) com a GTX 970.
**Validado no jogo (18/10/2026):** os três hooks foram instalados, o estéreo foi ativado, os FBOs 1920x1080
foram criados e a imagem lado a lado aparece corretamente.

## Fase 2: OpenXR (imagem no headset + rastreamento de cabeça)

Novos arquivos: `proxy/vr_xr.c` (backend OpenXR), `proxy/cod_vr.h` (declarações compartilhadas) e
`openxr_loader.dll` (Khronos 1.1.65, Win32), instalado ao lado do CoDSP.exe e carregado em tempo de execução.

- Runtimes 32 bits encontrados: `virtualdesktop-openxr-32.json` (ativo, VirtualDesktopXR 1.0.10, com
  `XR_KHR_opengl_enable`) e `oculus_openxr_32.json`.
- Sessão OpenXR criada sobre o próprio contexto GL do jogo, com espaço `LOCAL` (sentado) e uma swapchain
  por olho no tamanho recomendado. O formato é `GL_SRGB8_ALPHA8`, porque o jogo escreve cores já em gamma.
- `BeginFrame(LEFT)`: `xrWaitFrame`/`xrBeginFrame`/`xrLocateViews`. `SwapBuffers`: blit dos FBOs dos olhos
  para as swapchains, `xrEndFrame` com camada de projeção e espelho do olho esquerdo no monitor.
- `RenderScene`: orientação = yaw do corpo (yaw do jogo menos o yaw da cabeça já embutido no quadro) × pose do
  olho. Posição = `vieworg` + pose do olho × `WorldScale` (IPD vindo do runtime, 6DoF opcional). O fov
  simétrico é ampliado só para o culling (`R_SetupFrustum`).
- Projeção assimétrica: o `call` em `0x4cb216` vai para `hk_SetupProjection`, que chama o original e sobrescreve
  `m[0]`, `m[5]`, `m[8]` e `m[9]` das duas matrizes (`0x01536178` mundo, `0x015361b8` arma).
- Mira pela cabeça: o yaw é somado como delta em `cl.viewangles[YAW]`; o pitch é absoluto, menos um offset
  medido e filtrado entre o pitch previsto e `cl.viewangles` (o `delta_angles` do servidor).
- Sem headset: `xrGetSystem` é tentado de novo a cada 2 s; enquanto isso fica o modo lado a lado da fase 1.
- `vid_restart`: a sessão é destruída no `wglMakeCurrent(NULL)`, enquanto o contexto ainda existe, e recriada depois.

Testado fora do jogo: compilação sem warnings, passthrough (`gltest`), e `proxy/test/xrtest.c` (contexto GL
real + `vr_xr.c`) até o `xrGetSystem`, que retornou `XR_ERROR_FORM_FACTOR_UNAVAILABLE` porque não havia
headset conectado. **Ainda não testado com headset nem dentro do jogo.**

## Fase 3: HUD e menus numa tela flutuante (camada quad do OpenXR)

Novos endereços (backend do renderer):

| Função | Endereço | Como foi achada |
|---|---|---|
| `RB_SetGL2D` | `0x4ccc10` | único que chama `qglOrtho` (`0x1534aa0`); liga `backEnd.projection2D` (`0x01549b50`); 9 chamadores |
| `RB_BeginDrawingView` | `0x4cc050` | zera `projection2D` em `0x4cc08f` e lê `backEnd.refdef.rdflags` (`0x015496ec`) |

- Detours na entrada das duas funções (`JMP` + trampolim que reexecuta o prólogo). Os prólogos
  `C7 05 50 9B 54 01 01 00 00 00` e `51 A1 A8 46 53 01` são verificados antes do patch.
- No backend, para cada olho: vista 3D com mundo → FBO do olho; vista 3D sem mundo (`RDF_NOWORLDMODEL`) e todo
  2D → FBO do HUD (olho esquerdo) ou FBO descartável de 16×16 (olho direito, para não aplicar o blend duas vezes).
- O HUD é limpo com alpha 0 no início do olho esquerdo e enviado como `XrCompositionLayerQuad` com
  `BLEND_TEXTURE_SOURCE_ALPHA` (cores já multiplicadas pelo alpha, por causa do blend normal do jogo), preso à cabeça
  por padrão (`[HUD]` no ini: distância, largura, largura nos menus, offset vertical).
- Quadros sem mundo (menus, carregamento): envia só o quad; o monitor mostra o buffer do HUD.

**Validado no jogo com headset (18/10/2026):** hooks instalados, três swapchains (olhos 2112×2304, HUD 1920×1080),
sessão em FOCUSED sem erros de `xrEndFrame`, e HUD e menus aparecendo numa tela flutuante. Ajustes finos de
tamanho e posição ficam para depois.

## Fase 4: controles (mira, movimento, botões, arma na mão)

Novos endereços:

| O quê | Endereço | Como foi achado |
|---|---|---|
| `Cbuf_AddText` | `0x4209d0` | recebe o texto em **EAX**; chamado com `"snd_restart\n"`, `"quit\n"`, `"exec autoexec.cfg\n"` |
| `CL_JoystickMove` | `0x40a900` | `usercmd_t*` em **ECX**; lê `cl.joystickAxis` (`0x0142f2c0`); eixo 0 gira o yaw, eixo 1 → `forwardmove` (`cmd+0x14`) |
| `cl.joystickAxis[6]` | `0x0142f2c0` | gravado pelo `SE_JOYSTICK_AXIS` do `Com_EventLoop` (`0x42f1d0`) |
| `RE_AddRefEntityToScene` | `0x4dec10` | `(refEntity_t*, int)`; copia `0x27` dwords; a syscall `0x3e` do cgame passa 0 |
| `refEntity_t` | 0x9c bytes | `renderfx +4`, `lightingOrigin +0xc`, `axis +0x1c`, `origin +0x44`, `oldorigin +0x54` (iguais ao Q3) |

- OpenXR: action set `gameplay` com pose de mira, gatilho, grip, analógico e clique (por mão), A/B/X/Y e Menu.
  Bindings para `oculus/touch_controller` e `khr/simple_controller`. Sincronizado só em FOCUSED.
- Mira: do olho até o ponto a `ConvergeDistance` m ao longo do raio do controle (o tiro sai do olho do jogo e cruza
  a linha do cano nesse ponto). Escrita em `cl.viewangles` como na fase 2, com a mão no lugar da cabeça.
- Movimento: o detour do `CL_JoystickMove` (prólogo `A1 70 3C 42 01`) soma o analógico da outra mão ao `usercmd`,
  girado da direção da cabeça para a da mira. Giro: snap (padrão 45°) ou suave. Analógico para cima/baixo = postura.
- Botões: comandos do `[Buttons]` via `Cbuf_AddText` (`+cmd` fica pressionado até `-cmd`). `cg_drawCrosshair 0`
  quando a mira passa para a mão.
- Arma na mão: entidades com `RF_DEPTHHACK` (viewmodel com braços, clarão) são retidas no `AddRefEntityToScene` e
  liberadas no `RenderScene` do mundo, convertidas da vista do jogo (`refdef` real do quadro) para a pose da mão,
  menos `WeaponOffset*` (onde a arma fica em relação ao olho no jogo original).

Primeiro teste no jogo (18/10/2026): todos os hooks e as actions entraram; o Virtual Desktop aceitou os bindings Touch.

### Ajustes após o primeiro teste

- Controle esquerdo: o gatilho agora abre os objetivos (`+scores`); o grip segura a arma com as duas mãos.
  Grip direito = `+activate` (usar/pegar), Y = `+melee`.
- Duas mãos (`TwoHanded`): com o grip esquerdo pressionado à frente da mão da arma (até `TwoHandReach` m), o cano
  aponta da mão direita para a esquerda, mantendo o roll da mão direita. Vale para a mira e para o modelo da arma.
- Dados do jogo (pacotes opcionais em `main`, gerados por `mods/build_pk3.py` a partir dos originais):
  - `zz_cod_vr_scope.pk3`: `adsOverlayShader` vazio e `adsOverlayReticle none` nas 5 armas com luneta
    (`ui/assets/reticle_circle_quarter`), o que remove o retângulo preto da luneta. No CoD 1, ADS = `+speed`.
  - `zz_cod_vr_nohands.pk3`: os 16 `xmodel/viewmodel_hands*` passam a usar a skin `vr_nodraw@invisible.tga`, com o
    tipo de shader `shadertypes/model/vr_nodraw.stype` (`blendfunc GL_ZERO GL_ONE`). As mãos continuam no esqueleto
    (a arma segue presa aos ossos delas), só que invisíveis. Formato do xmodel: `... ff ff ff ff 00 00 00 00`, u16 com o
    número de skins e os nomes terminados em NUL; nomes de skin = `<tipo de shader>@<imagem em skins/>`.

Resultado no jogo: gatilho esquerdo (objetivos), duas mãos, grip direito e Y funcionaram; o overlay da luneta sumiu
(o `zz_cod_vr_scope.pk3` carrega), mas sem nada para mirar; as mãos continuaram visíveis.

### Segunda rodada

- Mãos: o tipo `vr_nodraw` (com `blendfunc GL_ZERO GL_ONE`) não teve efeito. Agora o pacote usa o tipo do próprio
  jogo `cloth_masked` (`alphaFunc GE128`) com uma TGA de alpha 0: `cloth_masked@vr_invisible.tga`.
  Conferido: os viewmodels das armas não têm superfícies de mão; só os 16 `viewmodel_hands*` têm.
- Luneta de verdade (`[Scope]`): enquanto o `fov_x` do `refdef` do jogo está abaixo de `MaxFov` (zoom de ADS com
  luneta, binóculos), o olho esquerdo renderiza uma vista extra do mundo a partir da lente (mão + offsets, ao longo
  do cano) com o fov de zoom do jogo. Ela vai para um FBO próprio no retângulo `Resolution`², que o backend reconhece
  pelo `backEnd.refdef` (`0x015496a0`: x=0, y=0, w=h=`Resolution`), e aparece como camada quad presa à arma.
  A vista reaproveita as entidades da cena restaurando os contadores `r_firstScene*` (`0x0154d72c`, `0x0154d718`,
  `0x0154d720`, `0x0154d724`) e é renderizada antes de a arma ser adicionada, para não tapar a lente.

Resultado: o overlay preto continuou e a lente mostrou um zoom sem utilidade; as mãos continuaram.

### Terceira rodada

- **Causa real das mãos e do overlay:** o CoDSP.exe 1.5 só monta `pak*.pk3` e `localized_*.pk3` de `main`; os `zz_*`
  nunca foram carregados (confirmado com `CoDSP.exe +set logfile 2 +path +quit`, que grava `main\console.log` na
  VirtualStore). O pacote das mãos agora é `pakz_vr_nohands.pk3` e aparece primeiro no search path. O pacote da luneta
  não é mais instalado.
- **Luneta (solução 1):** com zoom ativo, o olho esquerdo renderiza a vista do próprio jogo (olho do jogador, direção
  da mira, fov de zoom) dentro do buffer do HUD, por baixo do 2D do cgame (o overlay da luneta). A tela do HUD fica
  opaca e com `[Scope] Width`. O retículo do overlay marca onde o tiro vai. O backend reconhece essa vista por
  `x = 1` no `backEnd.refdef` (os olhos usam `x = 0`). A lente presa à arma da rodada anterior foi removida.
- **Botões:** gatilho esquerdo = `+speed` (ADS, que ativa o zoom da luneta), clique do analógico esquerdo = `+scores`.
- **Menus:** quando `cls.keyCatchers` (`0x01439264`) tem o bit da UI (2), o raio da mão da arma é intersectado com
  a tela do menu (presa à cabeça) e vira posição do cursor em 640×480. Os eventos são enfileirados na fila do motor
  (`eventQue` `0x89e478`, head `0x13f9e14`, tail `0x13f9e18`; `SE_MOUSE` relativo, com ressincronização a cada 2 s
  levando o cursor ao canto). Teclas via `SE_KEY`: gatilho = `K_MOUSE1` (200), B/Menu = `K_ESCAPE` (27),
  A = `K_ENTER` (13), analógico = setas (154/155). `CL_KeyEvent` = `0x40d000` (tecla na pilha, down em EBX,
  time em ECX), mas não é chamado direto: um clique pode carregar uma fase.

Resultado: mãos sumiram (com glitches ocasionais), objetivos e menus funcionam; o retângulo da luneta ainda aparecia;
aviso "too many visible models (more than 2045)".

### Quarta rodada

- **Passe único (headset):** o estéreo de dois passes do Q3 roda o cgame duas vezes por quadro e adiciona todos os
  modelos duas vezes, estourando o limite de 2045 entidades (`0x7fd` no `RE_AddRefEntityToScene`), o que faz modelos
  sumirem. Agora, com o headset, os flags de estéreo ficam desligados (o `SCR_UpdateScreen` desenha uma vez) e o
  `RenderScene` de cada cena de mundo renderiza os dois olhos, restaurando os contadores de entidades/luzes/polys
  entre eles. O olho direito é marcado para o backend com `x + 2` e a vista da luneta com `x + 1`. Sem headset,
  continua o modo de dois passes (lado a lado). Também corta o custo de CPU do cgame pela metade.
- **Bug corrigido:** o contador de draw surfaces (`0x0154d724`) não pode ser restaurado entre vistas, porque as
  superfícies da vista anterior ainda estão na fila do backend.
- Cenas de sky portal (`rdflags & 8`) não liberam a arma, não medem a mira e não renderizam a luneta.
- **Luneta:** com zoom, só a tela da luneta é enviada (sem o mundo em volta), a `[Scope] Distance` m e `Width` m.
- **Menus:** laser da mão até o ponto do menu, numa camada de projeção extra (meia resolução, com alpha) por cima de tudo.
- **Duas mãos:** `TwoHandGripHeight` (m) abaixa o ponto de apoio da mão esquerda em relação à linha do cano.
- **Diagnóstico** (`[Debug] LogNearEntities`): ao apertar o gatilho, registra por 6 quadros os modelos adicionados a
  menos de 120 unidades da vista (reType, renderfx, hModel, se foram presos à mão, posição na vista).

Diagnóstico de tiro (Thompson): o viewmodel é `reType 1`, `renderfx 0x8c`, na origem da vista. O clarão são
entidades `reType 4` e `13` com `RF_DEPTHHACK`, que o jogo põe na linha central da tela (≈ 31–48 à frente,
2,7 à direita, 2,6 abaixo), não no cano da arma do quadril. Correção: entidades presas que não são modelos mantêm a
distância ao longo do cano, mas a posição lateral vira `MuzzleOffsetRight`/`MuzzleOffsetUp` (padrão 0 = no eixo).

**Quarta rodada, com a correção do clarão, ainda não validada pelo que se vê no headset.**

### Quinta rodada

- **Letterbox:** nas missões o cgame reduz o `refdef` (faixas de cinema), e o buffer do olho, esticado para o headset,
  deixava tudo achatado na vertical. Agora os olhos sempre usam o buffer inteiro (x, y, largura e altura forçados).
- **Efeitos 2D de tela cheia** (tela vermelha de dano, fades, flashes): os `re.DrawStretchPic` (`0x4d4330`, offset
  `0x70`) que cobrem a tela inteira durante o jogo são guardados com a cor atual (`re.SetColor`, `0x4d42a0`, offset
  `0x6c`) e reemitidos no `re.EndFrame`, uma vez por olho. Antes de cada cópia vai uma cena vazia `NOWORLDMODEL`
  com `x = 3` (esquerdo) ou `x = 7` (direito), que diz ao backend para desenhar o 2D seguinte naquele olho.
- **Diagnóstico** de efeitos: uma linha por segundo com o número de cenas, o OR dos `rdflags` e o máximo de
  entidades/polys (`0x0154d70c`, `0x0154d730`) e dlights (`0x0154d714`) por quadro.

Resultado: proporções corretas nas missões; tela de dano e fades cobrem a visão. O log da granada mostrou 0–4 polys
por quadro: a explosão não chega por `AddPolyToScene`.

### Sexta rodada

- **Arma no rosto (artefatos ao andar):** quando o controle perde o tracking por um quadro, a arma deixava de ser presa
  à mão e era desenhada na posição da tela plana. Agora a última pose vale por até 1 s; depois disso, as entidades
  `RF_DEPTHHACK` são descartadas.
- **Roomscale** (`[VR] RoomScale`): a cada quadro, a distância horizontal cabeça–âncora vira `forwardmove`/`rightmove`
  (fecha a distância em ~0,2 s, com `g_speed` 190 e a regra `max(|f|,|r|)/127` do pmove). O deslocamento real do
  `vieworg`, na proporção do comando que era roomscale, move a âncora. Olhos e mãos são posicionados em relação à
  âncora, então o corpo do jogo (tiros, colisão, o que os NPCs veem) acompanha a cabeça, e as paredes continuam
  bloqueando. Saltos > 60 unidades (teleporte) ou distância > 1,5 m reiniciam a âncora.
- **Pegada:** `WeaponPitch`/`WeaponYaw` giram a arma em relação ao controle (com uma mão) e `AimSmoothing` filtra a
  direção da arma (constante de tempo em segundos).
- **Diagnóstico:** a linha `stats/s` inclui a contagem de entidades adicionadas por `reType`.

Resultado: inclinar na janela passou a acertar; a pegada ficou melhor; trancos ao andar com o analógico; NPCs
"implodidos" por menos de 1 s ao girar a cabeça devagar; clarão um pouco à direita; a Thompson parece imprecisa.
A contagem por `reType` mostrou que os efeitos (fumaça, explosões) chegam como sprites `reType 4`.

### Sétima rodada

- **Culling único:** cada olho usava seu próprio frustum simétrico. Um modelo pulado pelo olho esquerdo (sem
  atualizar o esqueleto no quadro) podia ser desenhado pelo direito, com os ossos colapsados. Agora os dois olhos usam
  a união dos dois FOVs + 4°.
- **LOD:** `tr.viewParms + 0x144` (`0x01536174`) é a escala de LOD de modelos (`tan(fov_x/2)/tan(40°)·r_lodScale`,
  usada no `0x4df2d0` junto com o bias em `0x01536170`). Como o FOV do VR é bem maior que o do jogo, isso escolhia
  LODs baixos. O `hk_SetupProjection` multiplica a escala por `tan(fov do jogo/2)/tan(fov de culling/2)`.
- **Roomscale** só com o analógico parado; andando com o analógico, a medição é descartada.
- **ADS automático** (`AutoADS`): arma a menos de 32 cm do olho e alinhada com o olhar (≈14°, solta com 40 cm/20°)
  envia `+speed`, para o jogo usar a precisão de mira em vez da dispersão do quadril. Com luneta, abre a tela da luneta.
- `MuzzleOffsetRight=-1.5` no ini para o clarão da Thompson.

**Resultado (18/10/2026):** tudo validado no jogo, exceto o ADS automático: a animação de ADS do jogo tira a arma
da posição das mãos. Ele ficou desligado por padrão (`AutoADS=0`); o ADS manual continua no gatilho esquerdo.

## Ainda não verificado

- Nada foi executado no jogo; os endereços vêm só da análise estática.
- `RE_BeginFrame` (`0x4d4680`) não foi reconhecido como função pelo Ghidra; a fronteira saiu da tabela do GetRefAPI.
- A matriz de projeção dentro de `R_RenderView` ainda não foi localizada.
- No cgamex86.dll, foram identificados só `vmMain`/`dllEntry` e os cvars `cg_gun_*` e `cg_bob*`.
