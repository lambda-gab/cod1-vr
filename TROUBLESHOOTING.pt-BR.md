# Solução de problemas

[English](TROUBLESHOOTING.md) | **Português**

Algo não funciona? Comece por aqui. Se nada abaixo resolver,
[envie um relatório de diagnóstico](#5-ainda-não-funciona-envie-um-relatório-de-diagnóstico): ele me mostra o que está
errado na sua configuração de VR.

## O jogo abre como duas imagens planas lado a lado (o VR não inicia)

É o que o jogo faz quando o OpenXR não conseguiu iniciar. Siga estes passos:

### 1. Atualize para a versão mais recente

Baixe o `CoD-VR-Setup.exe` da [release mais recente](https://github.com/lambda-gab/cod1-vr/releases/latest) e rode
de novo. Suas configurações são mantidas.

A versão 1.6.2 corrige uma causa comum: outros programas registram "API layers" do OpenXR (overlays e auxiliares de
outros jogos de VR) que são carregadas em todo programa OpenXR, e uma com arquivo ausente ou errado impedia o OpenXR
de iniciar (`XR_ERROR_FILE_ACCESS_ERROR`). O mod agora desliga essas layers para o jogo e tenta de novo.

### 2. Conecte o headset antes de abrir o jogo

- **Virtual Desktop:** abra o streamer no PC e conecte o headset.
- **Quest Link / Air Link:** inicie o link primeiro.
- **SteamVR:** inicie a SteamVR primeiro.

Depois abra o jogo pelo **launcher Call of Duty VR** (`CoD-VR-Launcher.exe`, ou o atalho da área de trabalho).

### 3. O jogo é de 32 bits: ele precisa de um runtime OpenXR de 32 bits

- O **Virtual Desktop** e a **Meta/Oculus** já incluem um.
- **SteamVR:** o runtime OpenXR de 32 bits só vem com o **beta da SteamVR** (Steam > SteamVR > Propriedades >
  Betas). Depois de instalar, defina a SteamVR como o runtime OpenXR nas configurações da SteamVR.

### 4. Um runtime precisa estar definido como o runtime OpenXR *ativo*

Ter um runtime instalado não basta: o Windows precisa ter um marcado como ativo. Se não houver, o jogo não acha o
headset (`XR_ERROR_RUNTIME_UNAVAILABLE`, e o relatório de diagnóstico mostra "no ActiveRuntime"). Defina no programa
que você usa para VR (o texto exato muda entre versões):

- **SteamVR:** Configurações (ative as configurações avançadas) > **Developer** > **Set SteamVR as OpenXR runtime**.
- **Meta Quest Link / app da Oculus:** Configurações > Geral > defina como o runtime OpenXR ativo.
- **Streamer do Virtual Desktop:** as configurações têm uma opção para defini-lo como o runtime OpenXR ativo.

Depois conecte o headset e abra o jogo de novo.

### 5. Ainda não funciona? Envie um relatório de diagnóstico

1. Abra o launcher (`CoD-VR-Launcher.exe`) e vá em **ABOUT**.
2. Clique em **"Problem starting in VR?"**. Ele salva o `CoD-VR-diagnostic.txt` na sua área de trabalho e mostra onde.
3. Envie esse arquivo para mim: no [Discord](https://discord.gg/SRsK2x5Tn) (em mensagem privada) ou em uma
   [issue no GitHub](https://github.com/lambda-gab/cod1-vr/issues). Diga qual é o seu headset e como você o conecta
   (Virtual Desktop, Link, SteamVR).

O relatório lista os runtimes OpenXR registrados no Windows para programas de 32 bits, pergunta ao próprio loader
OpenXR do jogo o que ele encontra (runtime, headset, suporte gráfico), lista as API layers do OpenXR, os programas de
VR em execução, sua placa de vídeo e driver, os arquivos do jogo, o `cod_vr.ini` e o último log do jogo
(`cod_vr.log`). Ele começa com um resumo das causas prováveis. Contém caminhos de arquivos (por exemplo seu nome de
usuário do Windows em `C:\Users\...`) e nada além disso de pessoal; leia antes de enviar, se quiser: é texto simples.

## Outros problemas

### A imagem no headset está esticada, com zoom ou preta (SteamVR e outros)

Corrigido na versão 1.5.0: o mod agora entrega as imagens dos olhos ao runtime no tamanho que o headset pede.
Atualize para a versão mais recente (passo 1 acima).

### As outras abas do launcher não respondem depois de abrir a aba VR

Corrigido na versão 1.6.1. Atualize para a versão mais recente.

### O jogo está lento ou trava

No launcher, **GRAPHICS**: diminua a **Headset resolution** (por exemplo para Medium, 85%) e o **Antialiasing**.
As mudanças precisam reiniciar o jogo. Feche outros programas que usam a placa de vídeo.

### "not CoDSP.exe 1.3" no `cod_vr.log`, ou o mod não faz nada

O mod é feito para o **Call of Duty 1.3** (`CoDSP.exe` de março de 2004). Outras versões do jogo guardam as coisas em
outros lugares, então o mod fica desligado. Atualize o jogo para a 1.3 primeiro. O instalador confere a versão para
você.

### A versão da Steam não abre

Abra pelo launcher: na versão da Steam ele inicia o jogo pela Steam, que o `CoDSP.exe` dela exige. A Steam precisa
estar aberta e com a conta conectada.

### Os controles ou botões não fazem nada

Confira se os controles do headset estão ligados e rastreados antes de abrir o jogo. Os botões estão listados no
[README](README.pt-BR.md#controles) e podem ser mudados em `[Buttons]` do `cod_vr.ini`.

### Outro problema

Pergunte no [Discord](https://discord.gg/SRsK2x5Tn) ou [abra uma issue](https://github.com/lambda-gab/cod1-vr/issues)
e anexe um relatório de diagnóstico se o problema for o VR não iniciar.
