#include "mx_web.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "mx_config.h"
#include "mx_log.h"
#include "mx_net.h"

static const char *TAG = "web";
static httpd_handle_t s_srv;
static bool s_on;
static char s_ssid[24];

static const char PAGE[] =
"<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>Maximus console</title><style>"
"body{margin:0;font:15px system-ui,sans-serif;background:#0b0e14;color:#e8ecf3}"
"main{max-width:900px;margin:0 auto;padding:16px}h1{font-size:20px;margin:4px 0 12px}"
"h2{font-size:15px;color:#8ea0b8;margin:18px 0 6px}.card{background:#151a24;border-radius:12px;padding:12px}"
"pre{margin:0;white-space:pre-wrap;word-break:break-all;font:12px ui-monospace,monospace;max-height:55vh;overflow:auto}"
"button{background:#2f7bff;color:#fff;border:0;border-radius:8px;padding:9px 14px;font-size:14px;margin:0 6px 6px 0}"
"button.g{background:#2a3242}#st{font-size:13px;color:#b8c4d6}.crash{color:#ff8a80}"
"</style></head><body><main><h1>Maximus OS console</h1>"
"<div class=card id=st>Loading...</div>"
"<h2>Last crash</h2><div class=card><pre id=crash>none</pre></div>"
"<h2>Live log</h2><div><button id=copy>Copy log</button><button class=g id=dl>Download</button>"
"<button class=g id=pause>Pause</button><button class=g id=clr>Clear view</button></div>"
"<div class=card><pre id=log></pre></div>"
"<h2>Log before the last restart</h2><div><button class=g id=copyp>Copy</button></div>"
"<div class=card><pre id=prev>none</pre></div>"
"<h2>Watch</h2><button class=g id=rst>Restart the watch</button>"
"</main><script>"
"const $=i=>document.getElementById(i);let pos=0,paused=false,text='';"
"function copy(t){const a=document.createElement('textarea');a.value=t;document.body.appendChild(a);a.select();"
"let ok=false;try{ok=document.execCommand('copy')}catch(e){}a.remove();"
"if(!ok&&navigator.clipboard)navigator.clipboard.writeText(t);}"
"function all(){return 'STATUS '+$('st').innerText+'\\nCRASH '+$('crash').innerText+'\\n--- log ---\\n'+text}"
"$('copy').onclick=()=>{copy(all());$('copy').textContent='Copied';setTimeout(()=>$('copy').textContent='Copy log',1500)};"
"$('copyp').onclick=()=>copy($('prev').innerText);"
"$('dl').onclick=()=>{const b=new Blob([all()],{type:'text/plain'});const a=document.createElement('a');"
"a.href=URL.createObjectURL(b);a.download='maximus-log.txt';a.click()};"
"$('pause').onclick=()=>{paused=!paused;$('pause').textContent=paused?'Resume':'Pause'};"
"$('clr').onclick=()=>{text='';$('log').textContent=''};"
"$('rst').onclick=()=>{if(confirm('Restart the watch?'))fetch('/restart',{method:'POST'})};"
"async function tick(){try{const r=await fetch('/log?from='+pos);const t=await r.text();"
"pos=+r.headers.get('X-Next')||pos;if(t){text+=t;if(text.length>400000)text=text.slice(-300000);"
"if(!paused){const e=$('log');const end=e.scrollTop+e.clientHeight>=e.scrollHeight-20;e.textContent=text;if(end)e.scrollTop=e.scrollHeight}}}"
"catch(e){}setTimeout(tick,1000)}"
"async function status(){try{const s=await (await fetch('/status')).json();"
"$('st').innerText=Object.entries(s).map(([k,v])=>k+': '+v).join('   ')}catch(e){}setTimeout(status,5000)}"
"fetch('/crash').then(r=>r.text()).then(t=>{if(t){$('crash').textContent=t;$('crash').className='crash'}});"
"fetch('/prev').then(r=>r.text()).then(t=>{if(t)$('prev').textContent=t});tick();status();"
"</script></body></html>";

static esp_err_t h_root(httpd_req_t *r)
{
    httpd_resp_set_type(r, "text/html");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    return httpd_resp_send(r, PAGE, sizeof PAGE - 1);
}

static esp_err_t h_log(httpd_req_t *r)
{
    char q[32], v[16];
    uint32_t pos = 0;
    if (httpd_req_get_url_query_str(r, q, sizeof q) == ESP_OK && httpd_query_key_value(q, "from", v, sizeof v) == ESP_OK)
        pos = strtoul(v, NULL, 10);
    enum { CAP = 16 * 1024 };
    char *b = heap_caps_malloc(CAP, MALLOC_CAP_SPIRAM);
    if (!b) return httpd_resp_send_500(r);
    size_t n = mx_log_read(&pos, b, CAP);
    char nx[16];
    snprintf(nx, sizeof nx, "%lu", (unsigned long)pos);
    httpd_resp_set_type(r, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(r, "X-Next", nx);
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    esp_err_t e = httpd_resp_send(r, b, n);
    free(b);
    return e;
}

static esp_err_t h_prev(httpd_req_t *r)
{
    size_t n = 0;
    const char *p = mx_log_prev(&n);
    httpd_resp_set_type(r, "text/plain; charset=utf-8");
    return httpd_resp_send(r, p ? p : "", p ? n : 0);
}

static esp_err_t h_crash(httpd_req_t *r)
{
    const char *c = mx_log_crash();
    httpd_resp_set_type(r, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(r, c ? c : "");
}

static esp_err_t h_status(httpd_req_t *r)
{
    mx_net_status_t st;
    mx_net_status(&st);
    char b[384];
    snprintf(b, sizeof b,
             "{\"firmware\":\"%s\",\"started\":\"%s\",\"uptime_s\":%lld,\"wifi\":\"%s\",\"ip\":\"%s\","
             "\"free_internal_kb\":%u,\"free_psram_kb\":%u,\"min_internal_kb\":%u}",
             MX_FW_VERSION, mx_reset_name(), (long long)(esp_timer_get_time() / 1000000), st.wifi ? st.ssid : "not connected",
             st.ip, (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
             (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024));
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_sendstr(r, b);
}

static void restart_later(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

static esp_err_t h_restart(httpd_req_t *r)
{
    httpd_resp_sendstr(r, "restarting");
    xTaskCreate(restart_later, "mx_rst", 2048, NULL, 5, NULL);
    return ESP_OK;
}

static bool server_start(void)
{
    if (s_srv) return true;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 6144;
    cfg.max_open_sockets = 4;
    cfg.lru_purge_enable = true;
    if (httpd_start(&s_srv, &cfg) != ESP_OK) { s_srv = NULL; return false; }
    const httpd_uri_t uris[] = {
        { .uri = "/", .method = HTTP_GET, .handler = h_root },
        { .uri = "/log", .method = HTTP_GET, .handler = h_log },
        { .uri = "/prev", .method = HTTP_GET, .handler = h_prev },
        { .uri = "/crash", .method = HTTP_GET, .handler = h_crash },
        { .uri = "/status", .method = HTTP_GET, .handler = h_status },
        { .uri = "/restart", .method = HTTP_POST, .handler = h_restart },
    };
    for (size_t i = 0; i < sizeof uris / sizeof *uris; i++) httpd_register_uri_handler(s_srv, &uris[i]);
    return true;
}

void mx_web_info(char *ssid, size_t ssid_n, char *pass, size_t pass_n)
{
    if (ssid) strlcpy(ssid, s_ssid, ssid_n);
    if (pass) strlcpy(pass, mx_cfg()->web_pass, pass_n);
}

bool mx_web_on(void) { return s_on; }

bool mx_web_set(bool on)
{
    mx_cfg_t *c = mx_cfg();
    if (on && strlen(c->web_pass) < 8) {
        snprintf(c->web_pass, sizeof c->web_pass, "%08lu", (unsigned long)(esp_random() % 100000000UL));
    }
    bool ok = true;
    if (on) {
        ok = mx_net_ap(true, s_ssid, c->web_pass) && server_start();
        if (!ok) ESP_LOGW(TAG, "web console did not start");
    } else {
        if (s_srv) { httpd_stop(s_srv); s_srv = NULL; }
        mx_net_ap(false, NULL, NULL);
    }
    s_on = on && ok;
    if (c->web_on != s_on || on) {
        c->web_on = s_on;
        mx_cfg_save();
    }
    if (s_on) ESP_LOGI(TAG, "web console on: join %s, open http://192.168.4.1", s_ssid);
    return ok;
}

void mx_web_init(void)
{
    uint8_t mac[6] = { 0 };
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(s_ssid, sizeof s_ssid, "Maximus-%02X%02X", mac[4], mac[5]);
    if (mx_cfg()->web_on) mx_web_set(true);
}
