/*
 * SPDX-License-Identifier: MIT
 */
#include "sd_features.h"
#include "sd_card.h"
#include <esp_heap_caps.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <cstring>
#include <string>

static const char* TAG = "SdWeb";
static httpd_handle_t _server = nullptr;

// Single-page UI. Talks to the JSON API below
static const char kIndexHtml[] = R"HTML(<!doctype html>
<html lang="pt-BR"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>StackChan</title>
<style>
body{font-family:system-ui,sans-serif;max-width:760px;margin:0 auto;padding:16px;background:#1a1028;color:#f3eefc}
h1{font-size:22px;margin:0 0 4px}small,.muted{color:#b9a8d6}
a{color:#ffb35c}nav{margin:12px 0;font-size:15px}
.row{display:flex;align-items:center;gap:8px;padding:8px 0;border-bottom:1px solid #3a2a55;flex-wrap:wrap}
.row .name{flex:1;min-width:160px;word-break:break-all}
button,.btn{background:#2c1d45;color:#f3eefc;border:1px solid #6b4a9a;border-radius:8px;padding:6px 10px;cursor:pointer;font-size:14px}
button.danger{border-color:#c0504d}audio{height:32px;max-width:100%}
.box{background:#24173a;border-radius:12px;padding:12px;margin:12px 0}
</style></head><body>
<h1>StackChan &middot; cart&atilde;o de mem&oacute;ria</h1>
<div class="muted" id="info"></div>
<nav id="crumbs"></nav>
<div class="box"><input type="file" id="files" multiple> <button onclick="upload()">Enviar para esta pasta</button>
<div class="muted" id="status"></div></div>
<div id="list"></div>
<div class="box muted" id="help"></div>
<script>
let dir='';
const HELP={'':'Pastas: <b>historias</b> (para ele contar), <b>vozes</b> (amostras de voz das crian&ccedil;as), <b>diario</b> (conversas por dia), <b>skins</b> (rostos novos), <b>backup</b>.',
'historias':'Arquivos <b>.ogg</b> em Opus, mono, 16 kHz. Pe&ccedil;a ao rob&ocirc;: &ldquo;conta a hist&oacute;ria do lobo&rdquo;.',
'skins':'Cada skin &eacute; uma pasta com <b>fundo.png</b> (320&times;240) e, opcional, <b>skin.json</b>: {"olhos":"#FFFFFF","palpebra":"#000000","boca":"#000000","presas":false}. Olhos ficam em (90,104) e (230,104); boca em (160,154).',
'controles':'Bot&otilde;es de controle remoto que o rob&ocirc; aprendeu. Pe&ccedil;a: &ldquo;aprende o bot&atilde;o de ligar a TV&rdquo; e depois &ldquo;liga a TV&rdquo;.',
'vozes':'Uma pasta por crian&ccedil;a. Pe&ccedil;a ao rob&ocirc;: &ldquo;grava a minha voz, meu nome &eacute; ...&rdquo;.'};
const q=s=>encodeURIComponent(s);
async function load(){
 const r=await fetch('/api/list?dir='+q(dir));const items=await r.json();
 const parts=dir?dir.split('/'):[];let c='<a href="#" onclick="go(\'\')">in&iacute;cio</a>',acc='';
 parts.forEach(p=>{acc+=(acc?'/':'')+p;const a=acc;c+=' / <a href="#" onclick="go(\''+a+'\')">'+p+'</a>'});
 document.getElementById('crumbs').innerHTML=c;
 document.getElementById('help').innerHTML=HELP[parts[0]||'']||'';
 let h='';
 items.forEach(it=>{const p=(dir?dir+'/':'')+it.name;
  if(it.dir){h+='<div class="row"><span class="name">&#128193; <a href="#" onclick="go(\''+p+'\')">'+it.name+'</a></span></div>';return;}
  const url='/api/file?path='+q(p);let player='';
  if(/\.(wav|ogg)$/i.test(it.name))player='<audio controls preload="none" src="'+url+'"></audio>';
  if(/\.txt$/i.test(it.name))player='<a class="btn" href="'+url+'" target="_blank">abrir</a>';
  h+='<div class="row"><span class="name">'+it.name+' <small>'+(it.size/1024).toFixed(1)+' KB</small></span>'+player+
  '<a class="btn" href="'+url+'&download=1">baixar</a><button class="danger" onclick="del(\''+p+'\')">apagar</button></div>';});
 document.getElementById('list').innerHTML=h||'<p class="muted">Pasta vazia.</p>';
 const i=await (await fetch('/api/info')).json();
 document.getElementById('info').textContent=i.free_mb+' MB livres de '+i.total_mb+' MB';
}
function go(d){dir=d;load();return false;}
async function del(p){if(!confirm('Apagar '+p+'?'))return;await fetch('/api/delete?path='+q(p),{method:'POST'});load();}
async function upload(){
 const fs=document.getElementById('files').files;const st=document.getElementById('status');
 for(const f of fs){st.textContent='Enviando '+f.name+'...';
  const r=await fetch('/api/upload?path='+q((dir?dir+'/':'')+f.name),{method:'POST',body:f});
  if(!r.ok){st.textContent='Falhou: '+f.name;return;}}
 st.textContent=fs.length?'Pronto!':'Escolha um arquivo.';load();
}
load();
</script></body></html>)HTML";

static void url_decode(char* s)
{
    char* out = s;
    for (char* in = s; *in; in++) {
        if (*in == '%' && in[1] && in[2]) {
            char hex[3] = {in[1], in[2], 0};
            *out++      = (char)strtol(hex, nullptr, 16);
            in += 2;
        } else if (*in == '+') {
            *out++ = ' ';
        } else {
            *out++ = *in;
        }
    }
    *out = 0;
}

// Resolve ?key= to an absolute path under /sdcard/stackchan, refusing anything that escapes it
static bool get_path(httpd_req_t* req, const char* key, std::string& out, bool allowEmpty = false)
{
    char query[512];
    char value[384] = {};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, key, value, sizeof(value));
    }
    url_decode(value);
    std::string rel = value;
    if ((!allowEmpty && rel.empty()) || rel.find("..") != std::string::npos || (!rel.empty() && rel[0] == '/')) {
        return false;
    }
    out = std::string(sd_paths::kRoot) + (rel.empty() ? "" : "/" + rel);
    return true;
}

static bool has_query_flag(httpd_req_t* req, const char* key)
{
    char query[512];
    char value[8];
    return httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
           httpd_query_key_value(query, key, value, sizeof(value)) == ESP_OK;
}

static std::string json_escape(const std::string& s)
{
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
        }
        if ((uint8_t)c >= 0x20) {
            out += c;
        }
    }
    return out;
}

static esp_err_t index_handler(httpd_req_t* req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, kIndexHtml, sizeof(kIndexHtml) - 1);
}

static esp_err_t info_handler(httpd_req_t* req)
{
    uint64_t total = 0;
    uint64_t free  = 0;
    {
        sd_card::BusGuard guard;
        esp_vfs_fat_info(sd_card::kMountPoint, &total, &free);
    }
    char json[64];
    snprintf(json, sizeof(json), "{\"total_mb\":%u,\"free_mb\":%u}", (unsigned)(total >> 20), (unsigned)(free >> 20));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t list_handler(httpd_req_t* req)
{
    std::string dir;
    if (!get_path(req, "dir", dir, true)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad path");
    }
    std::string json = "[";
    for (int pass = 0; pass < 2; pass++) {
        auto names = pass == 0 ? sd_paths::listDirs(dir.c_str()) : sd_paths::listFiles(dir.c_str());
        for (auto& name : names) {
            struct stat st = {};
            {
                sd_card::BusGuard guard;
                stat((dir + "/" + name).c_str(), &st);
            }
            if (json.size() > 1) {
                json += ",";
            }
            json += "{\"name\":\"" + json_escape(name) + "\",\"dir\":" + (pass == 0 ? "true" : "false") +
                    ",\"size\":" + std::to_string((long)st.st_size) + "}";
        }
    }
    json += "]";
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json.c_str());
}

static const char* content_type(const std::string& path)
{
    auto ends = [&](const char* ext) {
        size_t n = strlen(ext);
        return path.size() >= n && strcasecmp(path.c_str() + path.size() - n, ext) == 0;
    };
    if (ends(".wav")) return "audio/wav";
    if (ends(".ogg")) return "audio/ogg";
    if (ends(".txt")) return "text/plain; charset=utf-8";
    if (ends(".png")) return "image/png";
    if (ends(".json")) return "application/json";
    return "application/octet-stream";
}

static esp_err_t file_handler(httpd_req_t* req)
{
    std::string path;
    if (!get_path(req, "path", path)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad path");
    }
    FILE* f;
    {
        sd_card::BusGuard guard;
        f = fopen(path.c_str(), "rb");
    }
    if (!f) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
    }
    httpd_resp_set_type(req, content_type(path));
    std::string disposition;
    if (has_query_flag(req, "download")) {
        disposition = "attachment; filename=\"" + path.substr(path.rfind('/') + 1) + "\"";
        httpd_resp_set_hdr(req, "Content-Disposition", disposition.c_str());
    }

    auto* buf    = (char*)heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
    esp_err_t rc = buf ? ESP_OK : ESP_ERR_NO_MEM;
    size_t n = 0;
    while (rc == ESP_OK) {
        // Every SD transfer needs internal DMA memory; streaming a file in the middle of a conversation used to be
        // the one card access that didn't check (the SPI driver crashes when that memory runs out)
        if (!sd_card::waitDmaHeadroom()) {
            rc = ESP_FAIL;
            break;
        }
        {
            sd_card::BusGuard guard;
            n = fread(buf, 1, 4096, f);
        }
        if (n == 0) {
            break;
        }
        rc = httpd_resp_send_chunk(req, buf, n);  // Network send outside the guard
    }
    {
        sd_card::BusGuard guard;
        fclose(f);
    }
    heap_caps_free(buf);
    if (rc == ESP_OK) {
        rc = httpd_resp_send_chunk(req, nullptr, 0);
    }
    return rc;
}

static esp_err_t upload_handler(httpd_req_t* req)
{
    ESP_LOGI(TAG, "Upload start, %d bytes", req->content_len);
    std::string path;
    if (!get_path(req, "path", path)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad path");
    }
    FILE* f;
    {
        sd_card::BusGuard guard;
        f = fopen(path.c_str(), "wb");
    }
    if (!f) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "cannot create file");
    }

    auto* buf     = (char*)heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
    int remaining = req->content_len;
    bool ok       = buf != nullptr;
    int timeouts = 0;
    while (ok && remaining > 0) {
        int n = httpd_req_recv(req, buf, std::min(remaining, 4096));
        if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 3) {
            continue;  // A phone that went quiet for 45 s is gone: don't hold the server and the open file forever
        }
        timeouts = 0;
        if (n <= 0) {
            ok = false;
            break;
        }
        ok = sd_card::waitDmaHeadroom();
        if (ok) {
            sd_card::BusGuard guard;  // Receive from the network outside the guard, write inside
            ok = fwrite(buf, 1, n, f) == (size_t)n;
        }
        if (!ok) {
            break;
        }
        remaining -= n;
    }
    sd_card::BusGuard guard;
    fclose(f);
    heap_caps_free(buf);
    if (!ok) {
        remove(path.c_str());
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "upload failed");
    }
    ESP_LOGI(TAG, "Uploaded %s (%d bytes)", path.c_str(), req->content_len);
    return httpd_resp_sendstr(req, "ok");
}

static esp_err_t delete_handler(httpd_req_t* req)
{
    std::string path;
    if (!get_path(req, "path", path) || path.find("/.prepared") != std::string::npos) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad path");
    }
    sd_card::BusGuard guard;
    if (remove(path.c_str()) != 0 && rmdir(path.c_str()) != 0) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "cannot delete");
    }
    ESP_LOGI(TAG, "Deleted %s", path.c_str());
    return httpd_resp_sendstr(req, "ok");
}

void sd_web::start()
{
    if (_server || !sd_card::isMounted()) {
        return;
    }

    httpd_config_t config   = HTTPD_DEFAULT_CONFIG();
    config.stack_size       = 8192;
    config.task_caps        = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;  // Keep internal RAM for audio
    config.max_uri_handlers = 8;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 15;

    if (httpd_start(&_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Cannot start web server");
        _server = nullptr;
        return;
    }

    const httpd_uri_t routes[] = {
        {"/", HTTP_GET, index_handler, nullptr},
        {"/api/info", HTTP_GET, info_handler, nullptr},
        {"/api/list", HTTP_GET, list_handler, nullptr},
        {"/api/file", HTTP_GET, file_handler, nullptr},
        {"/api/upload", HTTP_POST, upload_handler, nullptr},
        {"/api/delete", HTTP_POST, delete_handler, nullptr},
    };
    for (auto& route : routes) {
        httpd_register_uri_handler(_server, &route);
    }
    ESP_LOGI(TAG, "Card web page started on port 80");
}
