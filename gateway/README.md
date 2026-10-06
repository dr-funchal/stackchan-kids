# Stack-Chan Gateway · Stack-Chan Home

Servidor de ferramentas extras (MCP) e painel da família para o Stack-Chan. Roda em Docker na VPS (`orbita-vps`,
`/var/www/stackchan-gateway`), atende em **https://m5.pulpfy.com** e **não substitui** a IA original do robô
(xiaozhi.me).

## Como funciona

```
Robô (CoreS3) ──(igual hoje)──► xiaozhi.me  (voz→texto, LLM, texto→voz)
     │                               ▲
     │ baixa músicas                 │ WebSocket de SAÍDA (MCP): o LLM vê e chama as ferramentas do gateway
     ▼                               │
https://m5.pulpfy.com ── nginx ──► Gateway (Docker, 127.0.0.1:3150)
                                     ├─ módulos: diagnóstico, histórias, música, Spotify, clima
                                     ├─ servidores MCP externos ligados pelo painel
                                     └─ painel "Stack-Chan Home" (login único)
```

- O gateway abre **uma conexão de saída** para o ponto de acesso MCP do agente no xiaozhi.me
  (`wss://api.xiaozhi.me/mcp/?token=…`, do app StackChan em **Configurações → MCP**).
- Se a VPS cair, as ferramentas extras somem e o robô continua conversando como sempre.
- O robô só fala com o gateway para **baixar músicas** (`/a/<código>`): códigos de uso único, válidos por 15 min,
  máximo 3 downloads. Nenhuma credencial fica no firmware.

## O painel (https://m5.pulpfy.com)

| Página | O que faz |
|---|---|
| Início | Conexão com o xiaozhi.me, última vez que o robô usou o gateway, contadores, "tocando agora" do Spotify, clima, atividade ao vivo, frases para as crianças experimentarem |
| Histórias | Criar, editar (ou importar `.txt`), pré-visualizar as páginas como o robô lê, apagar |
| Músicas | Enviar MP3/M4A/OGG/WAV/FLAC (até 60 MB): o gateway converte para o formato do robô e guarda |
| Spotify | Configurar o app (passo a passo), conectar a conta, aparelhos, tocar/transferir, buscar, volume |
| Conexões MCP | Adicionar servidores MCP da internet (HTTP streamable ou SSE), com cabeçalhos de autenticação; cada ferramenta começa desligada |
| Ferramentas | Ligar/desligar módulos e ferramentas; ver a descrição que a IA lê. Mudanças chegam ao robô em ~3 s |
| Atividade | Tudo o que o robô pediu e o que mudou no painel (filtros); não grava o que as crianças falam |
| Ajustes | Cidade de casa, destino padrão de "toca…", modo infantil, volume máximo, senha, sessões |

## Ferramentas que a IA do robô vê

| Ferramenta | Módulo | Para quê |
|---|---|---|
| `gateway_secret_word`, `gateway_get_time` | diagnostics | teste de ponta a ponta; hora |
| `library_search`, `library_read_page` | stories | biblioteca online, uma página por chamada |
| `play_music`, `music_list` | music | "toca X": robô (biblioteca própria) ou Spotify (aparelho da família) |
| `spotify_control`, `spotify_volume`, `spotify_devices`, `spotify_transfer`, `spotify_now_playing` | spotify | controle por voz |
| `weather_forecast` | weather | tempo agora e previsão (Open-Meteo, sem chave) |
| `ext_<servidor>_<ferramenta>` | MCP externo | o que você ligar em Conexões MCP |

No robô (firmware): `self.gateway.play_audio(code, name)` baixa a música para o cartão SD (fica em cache) e toca com
o player de histórias quando a conversa termina. Falar com o robô para a música.

## Música: robô × Spotify

- **No alto-falante do robô:** só a biblioteca enviada pelo painel. Cada arquivo é convertido uma vez para
  Ogg Opus mono 16 kHz, SILK banda larga, quadros de 60 ms (o único formato que o decodificador do robô aguenta, ver
  `CLAUDE.md`). O gateway confere pacote a pacote e remonta o arquivo sem o último pacote curto que o `opusenc` gera.
  Volume equalizado e graves cortados para o alto-falante pequeno.
- **Spotify:** o Spotify só toca em aparelhos Spotify Connect certificados (celular, computador, Echo, TV, caixas).
  O robô não pode ser um deles de forma legítima (exige o programa comercial de parceiros e certificação; clientes
  não oficiais violam os termos). O robô vira o controle remoto: "toca Galinha Pintadinha na sala".
- Spotify em 2026: o dono do app precisa de **Premium**; até 5 usuários por app; busca limitada a 10 itens.
- Modo infantil (padrão): nunca toca faixas explícitas, nem álbuns/playlists que contenham alguma. Volume máximo
  configurável.

### Ligar o Spotify

Siga a página **Spotify** do painel: criar app em https://developer.spotify.com/dashboard, Redirect URI
`https://m5.pulpfy.com/spotify/callback`, marcar Web API, colar o Client ID. Login com PKCE (sem segredo no servidor);
o refresh token fica criptografado.

## Segurança

- Login único (e-mail + senha em hash scrypt no `.env`); sessão em cookie `__Host-` HttpOnly, Secure, SameSite=Strict,
  guardada só como hash; 5 erros por endereço = bloqueio de 15 min (+ limite global e limite no nginx).
- CSRF: toda mudança exige a origem `https://m5.pulpfy.com`. CSP estrita (sem scripts/estilos inline), HSTS,
  anti-frame.
- Segredos (token do Spotify, cabeçalhos dos MCPs) criptografados com AES-256-GCM (`SECRETS_KEY`).
- MCPs externos: só endereços públicos (bloqueia localhost, redes privadas, metadados de nuvem), nunca executa
  programas; ferramentas externas começam desligadas.
- Uploads: só contêineres de áudio reconhecidos pelos primeiros bytes (uma playlist de texto poderia fazer o ffmpeg
  abrir outros arquivos), limite de tamanho, conversão uma por vez.
- Container: usuário 10001 (sem conta na VPS), raiz somente leitura, sem capabilities, 384 MB / 0,75 CPU, dados em
  `data/` (modo 700).

## Operação (no Mac, dentro de `gateway/`)

```bash
scripts/deploy.sh            # testa, envia, constrói e sobe (guarda a imagem anterior como :previous)
scripts/deploy.sh status     # estado do container e da conexão
scripts/deploy.sh logs       # últimos logs (LINES=300 scripts/deploy.sh logs)
scripts/deploy.sh rollback   # volta para a imagem anterior
scripts/deploy.sh stop       # derruba o gateway (o robô continua normal)
scripts/set-endpoint.sh      # cola o endereço MCP do app (fica escondido) e reinicia
scripts/update-nginx.sh      # instala deploy/nginx/* (backup + nginx -t + restauração automática)
```

- Trocar a senha: pelo painel (Ajustes). Para redefinir sem a senha atual: gere um hash com
  `read -rs PW && printf '%s' "$PW" | node scripts/hash-password.ts && unset PW`, troque `ADMIN_PASSWORD_HASH` no
  `.env` da VPS e apague `data/state/auth.json` (encerra as sessões).
- O deploy nunca apaga nem sobrescreve `data/` (histórias, músicas, ajustes, segredos) nem o `.env`.
- Desenvolvimento: `npm install`, `npm run check` (tipos), `npm test` (precisa de `opusenc` para os testes de áudio).

## Dados (`data/` na VPS)

```
historias/*.txt         biblioteca (primeira linha "# Título")
musicas/*.ogg           músicas já convertidas + library.json
state/settings.json     ajustes, módulos, servidores MCP (sem segredos)
state/secrets.json      segredos criptografados
state/auth.json         hash da senha trocada no painel + sessões (hash)
state/activity.json     últimas 300 atividades
```

## Fases

| Fase | O que | Situação |
|---|---|---|
| 1-5 | Arquitetura, gateway, registro de ferramentas, histórias | validado com o robô (palavra secreta, história de 3 páginas) |
| Painel | Stack-Chan Home com login, histórias, músicas, Spotify, MCPs, ferramentas, atividade, ajustes | no ar |
| 6 | Player no robô (`self.gateway.play_audio`) | firmware compilado; falta gravar no robô |
| 7 | Música: biblioteca própria no robô + Spotify nos aparelhos da família | no ar (robô depende da fase 6) |
| 8 | Outros MCPs | pelo painel, sem código |

## Riscos conhecidos

- O agente do robô fica na conta da M5Stack no xiaozhi.me; uma atualização em massa deles pode remover o ponto de
  acesso MCP. Se a Início mostrar "Reconectando…" por muito tempo, confira no app.
- Limites do xiaozhi.me (tempo de resposta, número de ferramentas) não são documentados: tempo limite padrão 8 s.
- A IA pode preferir as histórias do cartão do robô (`self.story.*`); as descrições `library_*` reforçam a biblioteca
  online (funcionou em 2026-10-05).
