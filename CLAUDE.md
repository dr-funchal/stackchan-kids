# StackChan (M5Stack CoreS3) — firmware oficial

Clone de https://github.com/m5stack/StackChan. O trabalho acontece em `firmware/` (ESP-IDF, C++).
`app/` (Flutter), `server/` (Go) e `remote/` (controle ESP-NOW) são projetos separados.
`gateway/` (Node/TypeScript, nosso) dá ferramentas extras à IA via ponto de acesso MCP do xiaozhi.me; ver seção abaixo.

## Gateway (`gateway/`)

- Roda em Docker na VPS `orbita-vps` (ssh já configurado), pasta `/var/www/stackchan-gateway`. Detalhes em `gateway/README.md`.
- Conexão só de saída para `wss://api.xiaozhi.me/mcp/?token=…`; VPS fora do ar = robô normal.
- Painel "Stack-Chan Home" em `https://m5.pulpfy.com` (nginx → `127.0.0.1:3150`), login único (hash scrypt no `.env`),
  DNS na Hostinger (a ferramenta MCP da Hostinger travou nesta máquina; a usuária edita DNS no hPanel).
- Comandos (em `gateway/`): `npm test`, `scripts/deploy.sh [deploy|status|logs|rollback|stop]`, `scripts/set-endpoint.sh`,
  `scripts/update-nginx.sh` (só os 2 arquivos do m5, com backup, `nginx -t` e restauração automática).
- **A VPS é compartilhada com sistemas de produção** (`evolu-ia`, `mcp-shopee`, `openclaw`): não mexa em outros containers,
  pastas nem em outros sites do nginx. O container roda como uid 10001 (o uid 1000 da VPS é de outro projeto).
- Segredos (token MCP, `SECRETS_KEY`, hash da senha) só no `.env` da VPS (chmod 600), nunca no chat, no git ou nos logs.
- Ferramentas pesam em toda fala do robô (a descrição vai para a IA do xiaozhi.me): o gateway expõe só 5 (com 12, as
  histórias longas engasgavam). Nomes não podem repetir os embutidos do xiaozhi.me: `play_music` gerava o alerta
  "Duplicate tool names" no robô (por isso `family_music`).
- **Limite de 32 ferramentas visíveis à IA no robô**: em 2026-10-09 são 31 (39 registradas, 8 `[user]` ficam ocultas).
  Antes de criar ferramenta nova no firmware, junte numa existente com parâmetro `action` (como `self.ir`, `self.poker`).
- Músicas no robô: ferramenta de firmware `self.gateway.play_audio` (`main/hal/utils/gateway_audio.cpp`) baixa de
  `https://m5.pulpfy.com/a/<código>` para `historias/musica_*.ogg` no cartão e toca com o player de histórias.

## Hardware

- Robô: M5Stack StackChan (CoreS3, ESP32-S3, 16 MB flash, 8 MB PSRAM)
- Porta USB: `/dev/cu.usbmodem14201` (às vezes muda para `14101`; USB-Serial/JTAG nativo, VID:PID 303a:1001, MAC 7c:4f:ad:ae:2e:58)
  - Se a porta mudar: `ls /dev/cu.usbmodem*`
- Não mova à mão partes ligadas aos servos com o robô ligado.

## Toolchain

- ESP-IDF **v5.5.4** em `~/esp/esp-idf-v5.5.4` (não é PlatformIO).
- O Python 3.9 do python.org não tem certificados SSL. Sempre exporte `SSL_CERT_FILE=/etc/ssl/cert.pem`
  antes de comandos do IDF que baixam coisas (install, component manager).

Ativar o ambiente (em cada shell novo):

```bash
export SSL_CERT_FILE=/etc/ssl/cert.pem && . ~/esp/esp-idf-v5.5.4/export.sh
```

## Comandos (rodar em `firmware/`)

```bash
python3 fetch_repos.py                          # baixa dependências (components/, xiaozhi-esp32/) e aplica o patch
idf.py build                                    # compilar
idf.py -p /dev/cu.usbmodem14201 flash           # gravar  ⚠️ PEDIR APROVAÇÃO À USUÁRIA ANTES
idf.py -p /dev/cu.usbmodem14201 monitor         # monitor serial (sair: Ctrl+])
idf.py -p /dev/cu.usbmodem14201 flash monitor   # gravar + monitorar  ⚠️ PEDIR APROVAÇÃO
idf.py size                                     # uso de flash/RAM
```

Testes que rodam no Mac (só a matemática de movimento):

```bash
cmake -S tests -B build-host-tests && cmake --build build-host-tests && ctest --test-dir build-host-tests --output-on-failure
```

## Regras de segurança

- **Nunca gravar no robô (`flash`, `app-flash`, `erase-flash`, `write_flash`) sem aprovação explícita da usuária em chat.**
  Compilar, rodar testes e monitorar são livres.
- Nunca rodar `idf.py erase-flash`: apaga NVS (Wi-Fi, conta, calibração).
- `idf.py flash` grava bootloader, tabela de partições, `otadata` (reiniciado), o app em `ota_0` e `generated_assets.bin`
  na partição `assets` (0xA00000). A NVS não é tocada. Para regravar só o app (mais rápido): `idf.py -p ... app-flash`.
- **Antes de um `flash` completo, confira `ls -la build/generated_assets.bin`: tem que ser ≤ 4 MB (4194304 bytes).**
  O `idf.py` não bloqueia; se passar, ele grava por cima de `coredump` e os assets ficam inválidos
  (`Assets: The index.json file is not found`, ícones faltando). Aconteceu em 2026-10-01 com a palavra de ativação
  custom "Lucas" (MultiNet7 inglês = assets de 4,77 MB), que além disso não funcionou. Mantenha `CONFIG_SR_WN_WN9_HISTACKCHAN_TTS3`.
- Se só o código mudou, prefira `app-flash` (só o app, ~25 s).
- Como o `otadata` é reiniciado, o boot passa a ser pelo `ota_0`. Um app da App Center que esteja em `ota_0`
  (hoje: flappy_bird) é sobrescrito.

## Backup do firmware de fábrica

`backup/factory_flash_16MB.bin` é a imagem completa da flash, tirada em 2026-09-30 (fora do git via `.git/info/exclude`).
`backup/app_ota0_antes_do_gateway_2026-10-09.bin`: só o app (ota_0, 0x20000) antes da gravação de 2026-10-09 (SHA-256 ao lado).
- ota_1: stack-chan **1.5.1** (IDF v5.5.4), o firmware ativo de fábrica; ota_0: app flappy_bird (App Center)
- SHA-256 em `backup/factory_flash_16MB.sha256`
- Contém a NVS (credenciais de Wi-Fi etc.): **não compartilhar nem commitar**.

Restaurar (só com aprovação):

```bash
python -m esptool --chip esp32s3 -p /dev/cu.usbmodem14201 -b 921600 write_flash 0 backup/factory_flash_16MB.bin
```

Alternativa oficial: M5Burner (ver https://docs.m5stack.com/en/StackChan).

## Arquitetura de `firmware/main`

- `main.cpp`: inicia a HAL, instala os apps no mooncake e roda o loop. Ao pedir o agente de IA, sai do loop
  e passa o controle ao xiaozhi (que nunca retorna).
- `apps/`: apps do launcher (framework mooncake + LVGL via `smooth_ui_toolkit`). Para criar um app, copie
  `app_template/`, registre em `apps/apps.h` e em `main.cpp` (`installApp`).
- `stackchan/`: o "personagem".
  - `modifiers/`: comportamentos combináveis (`blink`, `breath`, `head_pet`, `idle_expression`, `idle_motion`, `imu`, `speaking`, `dance`)
  - `avatar/skins/default/`: olhos, boca, balão de fala; `avatar/decorators/`: efeitos (`angry`, `dizzy`, `heart`, `shy`, `sweat`)
  - `motion/`, `animation/`, `addons/neon_light` (LEDs)
- `hal/`: hardware. `hal_servo`, `hal_head_touch`, `hal_imu`, `hal_rtc`, `hal_mcp` (ferramentas MCP do agente),
  `hal_espnow`, `hal_ota`, `board/` (integração com xiaozhi: display, câmera, áudio).
- Agente de IA: `xiaozhi-esp32/` (v2.2.4 + `patches/xiaozhi-esp32.patch`).

## Pegadinhas

- `xiaozhi-esp32/` e `components/` são **git-ignored** e recriados por `fetch_repos.py`. Mudanças neles precisam
  ir para `patches/xiaozhi-esp32.patch` (ou para `repos.json`), senão se perdem.
- `main/CMakeLists.txt` usa `GLOB_RECURSE`: ao **adicionar** arquivos `.cpp`, rode `idf.py reconfigure`.
- Configuração local (URL de servidor, OTA etc.) vai em `sdkconfig.defaults.local` (git-ignored, aplicado automaticamente).
- Idioma do agente: `CONFIG_LANGUAGE_EN_US` em `sdkconfig.defaults`.
- Muitos comentários no código estão em chinês.
- Versão do projeto: `PROJECT_VER` em `firmware/CMakeLists.txt`.
- Antes de commitar: `python3 scan_secrets.py` (procura segredos vazados).

## Armadilhas que já derrubaram o robô (corrupção de heap, `assert ... tlsf` / `StoreProhibited`)

Sintoma comum: reinício com backtrace numa tarefa de áudio (`AudioInputTask`, `OpusCodecTask`, `AfeWakeWord`).
Essas tarefas são só **vítimas**: o culpado escreveu fora da memória antes. Procure a mudança mais recente.

- **Nunca animar `lv_image_set_scale` a partir de 0.** Escala 0 faz o renderizador do LVGL escrever fora do buffer.
  Comece em ≥ 40 (`LV_SCALE_NONE` = 256 = 100%).
- **Sons (`PlaySound`) precisam ser Ogg Opus SILK banda larga, 60 ms, 1 quadro por pacote** (TOC config 11, como
  `xiaozhi-esp32/main/assets/common/*.ogg`). Gerar com:
  `opusenc --framesize 60 --bitrate 16 --set-ctl-int 4024=3001 --set-ctl-int 4008=1103 --set-ctl-int 4004=1103`.
  CELT/full-band ou pacotes multi-quadro (padrão do `opusenc`) estouram o decodificador.
- **Não tocar som (`PlaySound`) quando ele começa a escutar**, nem dentro de `SetStatus()` nem adiado com
  `Schedule`: corrompeu o heap todas as vezes (mic, AEC e wake word sendo ligados). Sons durante a fala
  (ex.: "crunch" do Papa-Letras) funcionam. O sinal de "pode falar" é visual (LEDs verdes do gesto Listen).
- **Opus `complexity` do encoder fica em 0**: 5 estourou a pilha da tarefa do codec.
- **Cartão SD e tela dividem o SPI3** (GPIO35 é MISO do SD e D/C da tela). Toda operação de arquivo no cartão
  precisa de `sd_card::BusGuard` (senão `assert spi_hal_setup_trans`).
- Ferramentas MCP rodam na **tarefa principal**: nada que espere (rede, IR, cartão grande) dentro delas; use tarefa
  própria (`xTaskCreatePinnedToCoreWithCaps(..., MALLOC_CAP_SPIRAM)`).
- Memória interna é escassa (mínimo já chegou a ~5 KB em conversas): pilhas de tarefas novas e buffers grandes na PSRAM.
- `ObjectPool` reaproveita IDs de modifiers: não guarde ID de modifier que se autodestrói (veja `GestureModifier::isActive`).

## Testar o robô

- Abrir/fechar a serial **reinicia** o robô. Leia o log numa janela longa, não em leituras curtas.
- O Mac não alcança o robô pela rede local (bloqueio de "Rede local"/VPN); teste a página web pelo celular.
- Decodificar backtrace: `xtensa-esp32s3-elf-addr2line -pfiaC -e build/stack-chan.elf <endereços>`.
