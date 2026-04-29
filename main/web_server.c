/**
 * @file web_server.c
 * @brief HTTP Web Server — JukeBox Dashboard
 *
 * Endpoints:
 *   GET  /            → Full HTML dashboard (embedded)
 *   GET  /api/status  → JSON player status
 *   GET  /api/list    → JSON playlist
 *   POST /api/play    → Play / Resume
 *   POST /api/pause   → Pause
 *   POST /api/stop    → Stop
 *   POST /api/next    → Next track
 *   POST /api/prev    → Previous track
 *   POST /api/auto    → Toggle auto-play
 *   POST /api/volume  → Set volume  {volume: 0-100}
 *   POST /api/play_file → Play specific file {filename: "name.mp3"}
 *   POST /upload      → Multipart file upload → saved to SD /sdcard/music/
 *   POST /api/delete  → Delete file {filename: "name.mp3"}
 */

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "jukebox.h"

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

static const char *TAG = "WEB_SERVER";

/* ─── Embedded HTML Dashboard ───────────────────────────── */
/* Dashboard HTML is large — stored as a C string literal.   */
static const char DASHBOARD_HTML[] =
    "<!DOCTYPE html>"
    "<html lang='en'>"
    "<head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>JukeBox ESP32</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:'Courier New',monospace;background:#0d0d0d;color:#f0f0f0;min-height:100vh}"
    "header{background:#111;border-bottom:2px solid #f5a623;padding:16px 24px;display:flex;align-items:center;gap:16px}"
    "header h1{font-size:1.6rem;letter-spacing:4px;color:#f5a623;text-transform:uppercase}"
    "header span{font-size:.75rem;color:#666;margin-left:auto}"
    ".container{max-width:860px;margin:0 auto;padding:24px}"
    ".card{background:#161616;border:1px solid #222;border-radius:8px;padding:20px;margin-bottom:20px}"
    ".card h2{font-size:.75rem;letter-spacing:3px;text-transform:uppercase;color:#f5a623;margin-bottom:16px}"
    "/* Now Playing */"
    "#now-playing{text-align:center;padding:28px 20px}"
    "#track-name{font-size:1.3rem;color:#fff;margin-bottom:6px;word-break:break-all}"
    "#track-state{font-size:.8rem;color:#f5a623;letter-spacing:2px;text-transform:uppercase}"
    "/* Controls */"
    ".controls{display:flex;gap:10px;justify-content:center;flex-wrap:wrap;margin-top:20px}"
    ".btn{background:#1e1e1e;border:1px solid #333;color:#f0f0f0;padding:10px 20px;border-radius:4px;"
    "     font-family:inherit;font-size:.85rem;letter-spacing:1px;cursor:pointer;transition:all .2s}"
    ".btn:hover{background:#f5a623;color:#000;border-color:#f5a623}"
    "/* Volume */"
    ".vol-row{display:flex;align-items:center;gap:12px;margin-top:16px}"
    ".vol-row label{font-size:.75rem;letter-spacing:2px;color:#888;width:60px}"
    "input[type=range]{flex:1;accent-color:#f5a623}"
    "#vol-val{width:36px;text-align:right;font-size:.85rem;color:#f5a623}"
    "/* Playlist */"
    ".playlist{list-style:none;max-height:280px;overflow-y:auto}"
    ".playlist::-webkit-scrollbar{width:4px}"
    ".playlist::-webkit-scrollbar-thumb{background:#333}"
    ".pl-item{display:flex;align-items:center;gap:10px;padding:8px 10px;border-radius:4px;cursor:pointer;transition:background .15s}"
    ".pl-item:hover{background:#1e1e1e}"
    ".pl-item.playing{background:#1a1500;border-left:3px solid #f5a623}"
    ".pl-idx{font-size:.7rem;color:#555;width:24px;text-align:right}"
    ".pl-name{flex:1;font-size:.85rem;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}"
    "/* Upload */"
    ".upload-zone{border:2px dashed #333;border-radius:8px;padding:32px;text-align:center;transition:border-color .2s}"
    ".upload-zone.drag{border-color:#f5a623}"
    ".upload-zone p{color:#555;font-size:.85rem;margin-bottom:12px}"
    "#file-input{display:none}"
    ".upload-label{background:#1e1e1e;border:1px solid #333;color:#f0f0f0;padding:8px 18px;"
    "              border-radius:4px;cursor:pointer;font-family:inherit;font-size:.8rem;display:inline-block}"
    ".upload-label:hover{border-color:#f5a623;color:#f5a623}"
    "#upload-progress{margin-top:12px;display:none}"
    "progress{width:100%;height:6px;accent-color:#f5a623;border-radius:3px}"
    "#upload-msg{font-size:.75rem;color:#888;margin-top:6px}"
    "/* Status bar */"
    "#status-bar{font-size:.72rem;color:#555;margin-top:4px;letter-spacing:1px}"
    ".dot{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:6px}"
    ".dot.green{background:#2ecc71}"
    ".dot.yellow{background:#f5a623}"
    ".dot.red{background:#e74c3c}"
    "</style>"
    "</head>"
    "<body>"
    "<header>"
    "  <span>&#9835;</span>"
    "  <h1>JukeBox</h1>"
    "  <span id='ip-info'>ESP32 LyraT-Mini</span>"
    "</header>"
    "<div class='container'>"

    "<!-- Now Playing -->"
    "<div class='card' id='now-playing'>"
    "  <div class='card h2' style='text-align:left'>NOW PLAYING</div>"
    "  <div id='track-name'>– – –</div>"
    "  <div id='track-state'>IDLE</div>"
    "  <div class='controls'>"
    "    <button class='btn' id='btn-play' onclick='playTrack()'>&#9654; PLAY</button>"
    "    <button class='btn' onclick='stopTrack()'>&#9632; STOP</button>"
    "  </div>"
    "  <div class='vol-row'>"
    "    <label>VOL</label>"
    "    <input type='range' min='0' max='100' value='60' id='vol-slider' oninput='setVolume(this.value)'>"
    "    <span id='vol-val'>60</span>"
    "  </div>"
    "  <div id='status-bar'><span class='dot red' id='dot'></span><span id='status-txt'>Connecting...</span></div>"
    "</div>"

    "<!-- Playlist -->"
    "<div class='card'>"
    "  <h2>PLAYLIST <span id='track-count' style='color:#555;font-weight:normal'></span></h2>"
    "  <ul class='playlist' id='playlist'><li style='color:#555;padding:8px'>Loading...</li></ul>"
    "</div>"

    "<!-- Upload -->"
    "<div class='card'>"
    "  <h2>UPLOAD AUDIO</h2>"
    "  <div class='upload-zone' id='drop-zone'>"
    "    <p>Drag &amp; drop MP3 / WAV / FLAC / AAC files here</p>"
    "    <label class='upload-label' for='file-input'>&#128193; Choose Files</label>"
    "    <input type='file' id='file-input' multiple accept='.mp3,.wav,.flac,.aac,.m4a' onchange='uploadFiles(this.files)'>"
    "  </div>"
    "  <div id='upload-progress'>"
    "    <progress id='prog-bar' value='0' max='100'></progress>"
    "    <div id='upload-msg'></div>"
    "  </div>"
    "</div>"

    "</div>" /* .container */

    "<script>"
    "let isPlaying = false;"

    "async function apiFetch(url, method = 'POST', body = null) {"
    "  try {"
    "    const opts = { method, headers: { 'Content-Type': 'application/json' } };"
    "    if (body) opts.body = JSON.stringify(body);"
    "    const r = await fetch(url, opts);"
    "    return await r.json();"
    "  } catch(e) { console.error(e); return null; }"
    "}"

    "async function playTrack() {"
    "  await apiFetch('/api/play');"
    "  setTimeout(refreshStatus, 200);"
    "}"

    "async function stopTrack() {"
    "  await apiFetch('/api/stop');"
    "  setTimeout(refreshStatus, 200);"
    "}"

    "async function setVolume(v) {"
    "  document.getElementById('vol-val').textContent = v;"
    "  await apiFetch('/api/volume', 'POST', { volume: parseInt(v) });"
    "}"

    "async function refreshStatus() {"
    "  try {"
    "    const r = await fetch('/api/status');"
    "    const d = await r.json();"
    "    if (!d) return;"
    "    isPlaying = (d.state === 'playing');"
    "    const name = d.current_file ? d.current_file.split('/').pop() : '– – –';"
    "    document.getElementById('track-name').textContent = name;"
    "    document.getElementById('track-state').textContent = d.state.toUpperCase();"
    "    const dot = document.getElementById('dot');"
    "    dot.className = 'dot ' + (d.state === 'playing' ? 'green' : (d.state === 'paused' ? 'yellow' : 'red'));"
    "    document.getElementById('status-txt').textContent ="
    "      'Track ' + (d.current_track + 1) + '/' + d.total_tracks + ' • Vol: ' + d.volume;"
    "    if (!document.getElementById('vol-slider').matches(':active')) {"
    "      document.getElementById('vol-slider').value = d.volume;"
    "      document.getElementById('vol-val').textContent = d.volume;"
    "    }"
    "    highlightCurrent(d.current_file);"
    "  } catch(e) { console.error(e); }"
    "}"

    "async function refreshPlaylist() {"
    "  try {"
    "    const r = await fetch('/api/list');"
    "    const d = await r.json();"
    "    if (!d) return;"
    "    const ul = document.getElementById('playlist');"
    "    document.getElementById('track-count').textContent = '(' + d.files.length + ')';"
    "    if (d.files.length === 0) {"
    "      ul.innerHTML = '<li style=\"color:#555;padding:8px\">No audio files on SD card</li>';"
    "      return;"
    "    }"
    "    ul.innerHTML = d.files.map((f, i) => {"
    "      const name = f.split('/').pop();"
    "      return `<li class='pl-item' onclick='playFile(\"${name}\")'>"
    "        <span class='pl-idx'>${i+1}</span>"
    "        <span class='pl-name'>${name}</span>"
    "      </li>`;"
    "    }).join('');"
    "  } catch(e) { console.error(e); }"
    "}"

    "async function playFile(filename) {"
    "  await apiFetch('/api/play_file', 'POST', { filename });"
    "  setTimeout(refreshStatus, 200);"
    "}"

    "function highlightCurrent(path) {"
    "  document.querySelectorAll('.pl-item').forEach(el => el.classList.remove('playing'));"
    "  if (!path) return;"
    "  const name = path.split('/').pop();"
    "  document.querySelectorAll('.pl-name').forEach(el => {"
    "    if (el.textContent === name) el.closest('.pl-item').classList.add('playing');"
    "  });"
    "}"

    "/* File upload */"
    "async function uploadFiles(files) {"
    "  const prog = document.getElementById('upload-progress');"
    "  const bar = document.getElementById('prog-bar');"
    "  const msg = document.getElementById('upload-msg');"
    "  prog.style.display = 'block';"
    "  for (let i = 0; i < files.length; i++) {"
    "    const f = files[i];"
    "    msg.textContent = 'Uploading: ' + f.name + ' (' + (i+1) + '/' + files.length + ')';"
    "    const fd = new FormData();"
    "    fd.append('file', f, f.name);"
    "    await new Promise((res, rej) => {"
    "      const xhr = new XMLHttpRequest();"
    "      xhr.open('POST', '/upload');"
    "      xhr.upload.onprogress = e => { if (e.lengthComputable) bar.value = Math.round(e.loaded / e.total * 100); };"
    "      xhr.onload = () => { bar.value = 100; res(); };"
    "      xhr.onerror = () => { msg.textContent = 'Error uploading ' + f.name; rej(); };"
    "      xhr.send(fd);"
    "    }).catch(() => {});"
    "  }"
    "  msg.textContent = 'Upload complete!';"
    "  bar.value = 100;"
    "  setTimeout(() => { prog.style.display = 'none'; }, 2000);"
    "  refreshPlaylist();"
    "}"

    "/* Drag-and-drop */"
    "const dz = document.getElementById('drop-zone');"
    "dz.addEventListener('dragover', e => { e.preventDefault(); dz.classList.add('drag'); });"
    "dz.addEventListener('dragleave', () => dz.classList.remove('drag'));"
    "dz.addEventListener('drop', e => {"
    "  e.preventDefault(); dz.classList.remove('drag');"
    "  uploadFiles(e.dataTransfer.files);"
    "});"

    "/* Boot */"
    "refreshStatus();"
    "refreshPlaylist();"
    "setInterval(refreshStatus, 2000);"
    "</script>"
    "</body>"
    "</html>";

/* ─── Helper: send JSON response ────────────────────────── */
static esp_err_t send_json(httpd_req_t *req, cJSON *root)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    char *json_str = cJSON_PrintUnformatted(root);
    esp_err_t ret = httpd_resp_sendstr(req, json_str);
    free(json_str);
    cJSON_Delete(root);
    return ret;
}

/* ─── GET / → Dashboard ─────────────────────────────────── */
static esp_err_t IRAM_ATTR handler_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, DASHBOARD_HTML, strlen(DASHBOARD_HTML));
    return ESP_OK;
}

/* ─── GET /api/status ────────────────────────────────────── */
static esp_err_t handler_status(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "state", player_state_str(g_player_state));
    cJSON_AddStringToObject(root, "current_file", g_current_file);
    cJSON_AddNumberToObject(root, "current_track", g_current_track);
    cJSON_AddNumberToObject(root, "total_tracks", g_total_tracks);
    cJSON_AddNumberToObject(root, "volume", g_volume);
    cJSON_AddBoolToObject(root, "auto_play", g_auto_play);
    return send_json(req, root);
}

/* ─── GET /api/list ──────────────────────────────────────── */
static esp_err_t handler_list(httpd_req_t *req)
{
    /* Re-scan so list is fresh after uploads */
    g_total_tracks = sd_scan_playlist();

    cJSON *root = cJSON_CreateObject();
    cJSON *files = cJSON_CreateArray();
    for (int i = 0; i < g_total_tracks; i++)
    {
        cJSON_AddItemToArray(files, cJSON_CreateString(g_playlist[i]));
    }
    cJSON_AddItemToObject(root, "files", files);
    return send_json(req, root);
}

/* ─── POST /api/play ─────────────────────────────────────── */
static esp_err_t handler_play(httpd_req_t *req)
{
    audio_send_cmd(CMD_PLAY);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "result", "ok");
    return send_json(req, r);
}

/* ─── POST /api/pause ────────────────────────────────────── */
static esp_err_t handler_pause(httpd_req_t *req)
{
    audio_send_cmd(CMD_PAUSE);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "result", "ok");
    return send_json(req, r);
}

/* ─── POST /api/stop ─────────────────────────────────────── */
static esp_err_t handler_stop(httpd_req_t *req)
{
    audio_send_cmd(CMD_STOP);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "result", "ok");
    return send_json(req, r);
}

/* ─── POST /api/next ─────────────────────────────────────── */
static esp_err_t handler_next(httpd_req_t *req)
{
    audio_send_cmd(CMD_NEXT);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "result", "ok");
    return send_json(req, r);
}

/* ─── POST /api/prev ─────────────────────────────────────── */
static esp_err_t handler_prev(httpd_req_t *req)
{
    audio_send_cmd(CMD_PREV);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "result", "ok");
    return send_json(req, r);
}

/* ─── POST /api/auto ─────────────────────────────────────── */
static esp_err_t handler_auto(httpd_req_t *req)
{
    if (g_auto_play)
    {
        audio_send_cmd(CMD_STOP_AUTO);
    }
    else
    {
        audio_send_cmd(CMD_AUTO_PLAY);
    }
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "result", "ok");
    cJSON_AddBoolToObject(r, "auto_play", !g_auto_play);
    return send_json(req, r);
}

/* ─── POST /api/volume  {volume: N} ─────────────────────── */
static esp_err_t handler_volume(httpd_req_t *req)
{
    char buf[64] = {0};
    int len = MIN(req->content_len, (int)sizeof(buf) - 1);
    httpd_req_recv(req, buf, len);

    cJSON *body = cJSON_Parse(buf);
    if (body)
    {
        cJSON *vol = cJSON_GetObjectItem(body, "volume");
        if (cJSON_IsNumber(vol))
        {
            audio_send_volume((int)vol->valuedouble);
        }
        cJSON_Delete(body);
    }
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "result", "ok");
    return send_json(req, r);
}

/* ─── POST /api/play_file  {filename: "x.mp3"} ──────────── */
static esp_err_t handler_play_file(httpd_req_t *req)
{
    char buf[MAX_FILENAME_LEN + 32] = {0};
    int len = MIN(req->content_len, (int)sizeof(buf) - 1);
    httpd_req_recv(req, buf, len);

    cJSON *body = cJSON_Parse(buf);
    if (body)
    {
        cJSON *fn = cJSON_GetObjectItem(body, "filename");
        if (cJSON_IsString(fn) && fn->valuestring)
        {
            audio_send_play_file(fn->valuestring);
        }
        cJSON_Delete(body);
    }
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "result", "ok");
    return send_json(req, r);
}

/* ─── POST /api/delete  {filename: "x.mp3"} ─────────────── */
static esp_err_t handler_delete(httpd_req_t *req)
{
    char buf[MAX_FILENAME_LEN + 32] = {0};
    int len = MIN(req->content_len, (int)sizeof(buf) - 1);
    httpd_req_recv(req, buf, len);

    cJSON *body = cJSON_Parse(buf);
    bool ok = false;
    if (body)
    {
        cJSON *fn = cJSON_GetObjectItem(body, "filename");
        if (cJSON_IsString(fn) && fn->valuestring)
        {
            char path[MAX_FILENAME_LEN];
            snprintf(path, sizeof(path), "%s/%s", AUDIO_DIR, fn->valuestring);
            if (unlink(path) == 0)
            {
                ok = true;
                ESP_LOGI(TAG, "Deleted: %s", path);
                g_total_tracks = sd_scan_playlist();
            }
        }
        cJSON_Delete(body);
    }
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "result", ok ? "ok" : "error");
    return send_json(req, r);
}

/* ─── POST /upload — multipart file upload to SD ────────── */
static esp_err_t handler_upload(httpd_req_t *req)
{
    char filename[128] = "upload.mp3";
    char dest[MAX_FILENAME_LEN];

    // 1. Extract filename from Content-Disposition header
    char hdr[256] = {0};
    if (httpd_req_get_hdr_value_str(req, "Content-Disposition", hdr, sizeof(hdr)) == ESP_OK)
    {
        char *fn = strstr(hdr, "filename=\"");
        if (fn)
        {
            fn += 10;
            char *end = strchr(fn, '"');
            if (end)
            {
                int n = end - fn;
                if (n > 0 && n < (int)sizeof(filename) - 1)
                {
                    strncpy(filename, fn, n);
                    filename[n] = '\0';
                }
            }
        }
    }

    snprintf(dest, sizeof(dest), "%s/%s", AUDIO_DIR, filename);
    ESP_LOGI(TAG, "Uploading to %s (%d bytes)", dest, req->content_len);

    // 2. Open file for writing
    FILE *f = fopen(dest, "wb");
    if (!f)
    {
        ESP_LOGE(TAG, "Failed to open file for writing");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Cannot write to SD card");
        return ESP_FAIL;
    }

    // 3. Read the request body chunk by chunk and write directly to file
    char *buf = malloc(1024); // small buffer for efficiency
    if (!buf)
    {
        fclose(f);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }

    int remaining = req->content_len;
    int total_written = 0;

    while (remaining > 0)
    {
        int to_read = (remaining < 1024) ? remaining : 1024;
        int ret = httpd_req_recv(req, buf, to_read);
        if (ret <= 0)
        {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT)
                continue;
            ESP_LOGE(TAG, "Socket error %d", ret);
            break;
        }
        // Write the raw data – no multipart boundary stripping needed for simple uploads
        size_t written = fwrite(buf, 1, ret, f);
        if (written != ret)
        {
            ESP_LOGE(TAG, "Short write to SD card");
            break;
        }
        total_written += ret;
        remaining -= ret;
    }

    fclose(f);
    free(buf);

    if (total_written == 0)
    {
        ESP_LOGE(TAG, "No data written to %s", dest);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Upload failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Uploaded %d bytes to %s", total_written, dest);

    // Refresh playlist
    g_total_tracks = sd_scan_playlist();

    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "result", "ok");
    cJSON_AddStringToObject(r, "filename", filename);
    cJSON_AddNumberToObject(r, "size", total_written);
    char *resp = cJSON_PrintUnformatted(r);
    httpd_resp_sendstr(req, resp);
    free(resp);
    cJSON_Delete(r);
    return ESP_OK;
}

/* ─── Register all URI handlers ──────────────────────────── */
static const httpd_uri_t uri_handlers[] = {
    {.uri = "/", .method = HTTP_GET, .handler = handler_root},
    {.uri = "/api/status", .method = HTTP_GET, .handler = handler_status},
    {.uri = "/api/list", .method = HTTP_GET, .handler = handler_list},
    {.uri = "/api/play", .method = HTTP_POST, .handler = handler_play},
    {.uri = "/api/stop", .method = HTTP_POST, .handler = handler_stop},
    {.uri = "/api/volume", .method = HTTP_POST, .handler = handler_volume},
    {.uri = "/api/play_file", .method = HTTP_POST, .handler = handler_play_file},
    {.uri = "/upload", .method = HTTP_POST, .handler = handler_upload},
};

/* ─── Public Init ────────────────────────────────────────── */
esp_err_t web_server_init(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = WEB_SERVER_PORT;
    cfg.stack_size = 10240;
    cfg.max_uri_handlers = 16;
    cfg.recv_wait_timeout = 30;
    cfg.send_wait_timeout = 30;
    cfg.max_resp_headers = 8;
    cfg.uri_match_fn = httpd_uri_match_wildcard;

    cfg.stack_size = 4096;       // Reduce from default 8192
    cfg.task_priority = 5;       // Lower priority
    cfg.backlog_conn = 5;        // Reduce connections
    cfg.lru_purge_enable = true; // Enable LRU purge

    httpd_handle_t server = NULL;
    esp_err_t ret = httpd_start(&server, &cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }

    for (int i = 0; i < sizeof(uri_handlers) / sizeof(uri_handlers[0]); i++)
    {
        httpd_register_uri_handler(server, &uri_handlers[i]);
    }

    ESP_LOGI(TAG, "HTTP server started — %d endpoints registered",
             (int)(sizeof(uri_handlers) / sizeof(uri_handlers[0])));
    return ESP_OK;
}
