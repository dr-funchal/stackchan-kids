# Stack-Chan Gateway

Servidor de ferramentas extras (MCP) para o Stack-Chan. Roda em Docker na VPS (`orbita-vps`,
`/var/www/stackchan-gateway`) e **não substitui** a IA original do robô (xiaozhi.me).

## Como funciona

```
Robô (CoreS3) ──(igual hoje)──► xiaozhi.me  (voz→texto, LLM, texto→voz)
                                     ▲
                                     │ WebSocket de SAÍDA (MCP)
                                     │
                           Gateway na VPS ──► módulos (diagnóstico, histórias, ...)
```

- O gateway abre **uma conexão de saída** para o "ponto de acesso MCP" do agente no xiaozhi.me
  (`wss://api.xiaozhi.me/mcp/?token=…`, gerado no app StackChan em **Configurações → MCP**).
- O LLM do xiaozhi.me passa a ver as ferramentas do gateway junto com as do robô (`self.*`) e as chama quando
  precisa. A resposta volta pela voz original do robô.
- **O firmware não muda.** Se a VPS cair, as ferramentas extras somem e o robô continua como sempre.
- O único segredo é o token do endereço, guardado em `.env` (chmod 600).
- Lado HTTP: **https://m5.pulpfy.com** → nginx da VPS → container (porta 3150, só em `127.0.0.1`). Hoje só tem
  `/health`; na fase 6 servirá o áudio. Configuração em `deploy/nginx/`, instalada por `scripts/setup-nginx.sh`
  (precisa do registro DNS `A m5 → IP da VPS` na Hostinger).

## Ferramentas

| Ferramenta | Módulo | O que faz |
|---|---|---|
| `gateway_secret_word` | diagnostics | Teste ponta a ponta: devolve uma palavra sorteada que só existe no log do gateway |
| `gateway_get_time` | diagnostics | Data e hora (fuso `GATEWAY_TIMEZONE`) |
| `story_list` | stories | Lista histórias da biblioteca, com filtro por tema |
| `story_read_page` | stories | Uma página por vez (~750 caracteres), com regras de leitura em voz alta |

Cada chamada passa pelo registro (`src/registry/registry.ts`): validação dos argumentos (JSON Schema),
tempo limite, limite de chamadas por minuto, erros sem detalhes internos para o LLM e log sem o conteúdo das falas
(só os nomes dos campos, em nível `info`). Ferramentas marcadas `risk: 'restricted'` ficam escondidas, a menos que
`ALLOW_RESTRICTED_TOOLS=true`.

### Biblioteca de histórias

Arquivos `.txt` em `data/historias/` na VPS (montado só para leitura). A primeira linha pode ser `# Título`.
Para adicionar: copie o arquivo para lá. Não precisa reiniciar, porque a pasta é lida a cada pedido. O `deploy` envia
as histórias deste repositório, mas nunca apaga as que já estão na VPS.

### Adicionar um módulo novo

1. Crie `src/modules/<nome>.ts` exportando uma função que devolve um `GatewayModule` (veja `diagnostics.ts`).
2. Registre em `src/modules/index.ts` (uma linha).
3. Inclua o nome em `MODULES` no `.env` da VPS e rode `scripts/deploy.sh`.

Nomes de ferramentas: `snake_case`, únicos. A descrição é escrita **para o LLM** e diz *quando* chamar.

## Operação (rodar no Mac, dentro de `gateway/`)

```bash
scripts/deploy.sh            # testa, envia, constrói e sobe (guarda a imagem anterior como :previous)
scripts/deploy.sh status     # estado do container e da conexão
scripts/deploy.sh logs       # últimos logs (LINES=300 scripts/deploy.sh logs)
scripts/deploy.sh rollback   # volta para a imagem anterior
scripts/deploy.sh stop       # derruba o gateway (o robô continua normal)
scripts/set-endpoint.sh      # cola o endereço MCP do app (fica escondido) e reinicia
scripts/setup-nginx.sh       # uma vez: site m5.pulpfy.com no nginx (com nginx -t antes) + certificado TLS
```

Desligar sem remover: `GATEWAY_ENABLED=false` no `.env` da VPS + `docker compose up -d`.

Desenvolvimento: `npm install`, `npm run check` (tipos), `npm test`.

Estados em `status`: `standby` (sem endereço ou desligado), `connecting`, `connected`, `backoff` (esperando
para reconectar; o intervalo dobra até 60 s).

## Plano por fases

| Fase | O que | Firmware? | Situação |
|---|---|---|---|
| 1-2 | Auditoria e arquitetura (ponto de acesso MCP do xiaozhi.me) | não | feito |
| 3 | Gateway em Docker na VPS + teste da palavra secreta | não | código no ar; falta o endereço do app |
| 4 | Registro modular de ferramentas (validação, limites, política) | não | feito |
| 5 | Histórias da biblioteca na VPS | não | feito (1 história de exemplo) |
| 6 | Player de áudio por streaming no robô | **sim** | a planejar |
| 7 | Música (biblioteca própria, não Spotify) | sim (usa a fase 6) | a planejar |
| 8 | Outros MCPs (clima, agenda, casa) | não | quando quiser |

### Teste da fase 3

1. App StackChan → Configurações → MCP → copie o endereço `wss://…`.
2. `scripts/set-endpoint.sh` e cole (não aparece na tela).
3. `scripts/deploy.sh status` deve mostrar `"state":"connected"`. No app, a tela MCP lista as ferramentas.
4. Pergunte ao robô: *"qual é a palavra secreta do gateway?"*.
5. `scripts/deploy.sh logs | grep "secret word"`: a palavra do log tem que ser a que o robô falou.

### Fases 6-7 (áudio), resumo do que falta decidir

- Áudio **não passa** pelo ponto de acesso MCP (só texto). O robô vai precisar baixar o áudio direto do gateway:
  uma ferramenta nova no firmware (`self.audio.play_url`) que recebe uma URL **curta, de uso único e com validade de
  minutos**, gerada pelo gateway. Assim nenhuma credencial fica no firmware.
- Isso exige porta HTTPS na VPS (via o nginx que já existe, num subdomínio), e aí entram TLS e limite de taxa de
  verdade.
- O gateway converte o arquivo para Ogg Opus no formato que o robô já decodifica. Nenhum decodificador novo no ESP32
  (memória interna é o ponto fraco, ver `CLAUDE.md`).
- Parar: tocar na cabeça (confiável) + palavra de ativação (falha com som alto tocando).
- Spotify não entrega áudio para dispositivos de terceiros: a Web API só controla aparelhos Spotify Connect, e virar
  um exige o programa comercial de parceiros. A música vem de uma biblioteca própria (MP3 comprado ou licenciado) na VPS.
- Gravar firmware no robô exige aprovação explícita (ver `CLAUDE.md`).

## Riscos conhecidos

- O agente do robô fica na conta da M5Stack no xiaozhi.me. O servidor deles já teve um script que zerou
  `McpEndpoints` de todos os agentes (`server/internal/xiaozhi/xiaozhi.go`). Se o gateway ficar em `backoff` sem motivo,
  confira no app se o ponto de acesso continua lá.
- Limites do xiaozhi.me (quanto tempo espera uma ferramenta, quantas ferramentas aceita) não estão documentados.
  Por isso o tempo limite padrão é 8 s.
- O LLM pode escolher as histórias do cartão SD do robô (`self.story.play`) em vez das do gateway. As descrições
  ajudam, mas não garantem.
