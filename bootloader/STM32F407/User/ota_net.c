#include "ota.h"
#include "./BSP/ATK_MW8266D/atk_mw8266d.h"
#include "./BSP/ATK_MW8266D/atk_mw8266d_uart.h"
#include "./SYSTEM/usart/usart.h"
#include "./SYSTEM/delay/delay.h"

/* ???? */
Dev_t D;
char g_resp[RESP_SZ];
char g_msg[64];

static char g_req[512];
static char g_body[256];

static char cs_leftover[512];
static int  cs_leftover_len = 0;



/* ==================== ?? ==================== */

static uint8_t hv(char c)
{
    if(c>='0'&&c<='9') return c-'0';
    if(c>='a'&&c<='f') return c-'a'+10;
    if(c>='A'&&c<='F') return c-'A'+10;
    return 0;
}

static bool jgs(const char *j, const char *k, char *b, size_t bl)
{
    char s[64]; const char *p,*q; size_t i;
    if(!j||!k||!b||!bl) return false;
    snprintf(s,64,"\"%s\":",k);
    p=strstr(j,s); if(!p) return false;
    q=p+strlen(s);
    while(*q==' '||*q=='\t'||*q=='\r'||*q=='\n') q++;
    if(*q!='"') return false;
    q++; i=0;
    while(*q!='"'&&*q&&i<bl-1){
        if(*q=='\\'&&(*(q+1)=='\\'||*(q+1)=='"')) q++;
        b[i++]=*q++;
    }
    b[i]='\0';
    return true;
}

static bool jgi(const char *j, const char *k, int *v)
{
    char s[64]; const char *p,*q; char *e;
    if(!j||!k||!v) return false;
    snprintf(s,64,"\"%s\":",k);
    p=strstr(j,s); if(!p) return false;
    q=p+strlen(s);
    while(*q==' '||*q=='\t'||*q=='\r'||*q=='\n') q++;
    *v=(int)strtol(q,&e,10);
    return e!=q;
}

static bool jgb(const char *j, const char *k, bool *v)
{
    char s[64]; const char *p,*q;
    if(!j||!k||!v) return false;
    snprintf(s,64,"\"%s\":",k);
    p=strstr(j,s); if(!p) return false;
    q=p+strlen(s);
    while(*q==' '||*q=='\t'||*q=='\r'||*q=='\n') q++;
    if(strncasecmp(q,"true",4)==0){*v=true;return true;}
    if(strncasecmp(q,"false",5)==0){*v=false;return true;}
    return false;
}

static void uart_drain(uint32_t ms)
{
    uint32_t t=HAL_GetTick();
    while((HAL_GetTick()-t)<ms){
        if(atk_mw8266d_uart_rx_get_frame_timeout(30)==NULL) break;
        atk_mw8266d_uart_rx_restart();
    }
}

/* ==================== WiFi ==================== */

bool wifi_connect(void)
{
    uint32_t t;
    bool ok = false;
    char cmd[128];

    printf("[WIFI] ssid=%s\r\n", D.ssid);

    atk_mw8266d_send_at_cmd("AT", "OK", 2000);
    delay_ms(200);
    atk_mw8266d_send_at_cmd("AT+CWMODE=1", "OK", 2000);
    delay_ms(200);
    atk_mw8266d_send_at_cmd("AT+CIPMUX=0", "OK", 2000);
    delay_ms(200);

    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", D.ssid, D.pass);
    printf("[WIFI] CMD: %s\r\n", cmd);
    atk_mw8266d_uart_rx_restart();
    atk_mw8266d_uart_printf("%s\r\n", cmd);

    t = HAL_GetTick();
    while((HAL_GetTick() - t) < 25000)
    {
        uint8_t *f = atk_mw8266d_uart_rx_get_frame_timeout(3000);
        if(f)
        {
            printf("[WIFI] %s\r\n", (char*)f);
            if(strstr((char*)f, "FAIL")){
                printf("[WIFI] connect FAIL\r\n");
                return false;
            }
            if(strstr((char*)f, "OK")){
                ok = true;
                break;
            }
        }
        atk_mw8266d_uart_rx_restart();
    }

    if(!ok){
        printf("[WIFI] timeout\r\n");
        return false;
    }

    printf("[WIFI] connected\r\n");
    delay_ms(2000);
    uart_drain(500);
    return true;
}

/* ==================== TCP ==================== */

static bool tcp_conn(void)
{
    char cmd[128];
    uint8_t ret;
    int i;

    for(i = 0; i < 3; i++)
    {
        printf("[TCP] attempt %d/3\r\n", i + 1);

        atk_mw8266d_send_at_cmd("AT+CIPCLOSE", "OK", 3000);
        delay_ms(1000);

        snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%d", SERVER_IP, SERVER_PORT);
        printf("[TCP] %s\r\n", cmd);

        ret = atk_mw8266d_send_at_cmd(cmd, "OK", 15000);
        printf("[TCP] ret=%d\r\n", ret);

        if(ret == 0)
        {
            printf("[TCP] connected\r\n");
            delay_ms(500);
            return true;
        }

        printf("[TCP] failed\r\n");
        delay_ms(2000);
    }

    return false;
}

/* ==================== CIPSEND ==================== */

static bool cipsend(const char *data, int len)
{
    char cmd[32];
    uint32_t t;

    /* ??????? */
    cs_leftover_len = 0;
    memset(cs_leftover, 0, sizeof(cs_leftover));

    printf("[SEND] len=%d\r\n", len);

    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d", len);
    printf("[SEND] %s\r\n", cmd);

    atk_mw8266d_send_at_cmd(cmd, "OK", 5000);
    printf("[SEND] CIPSEND done\r\n");

    delay_ms(200);
    atk_mw8266d_uart_rx_restart();
    delay_ms(50);

    atk_mw8266d_uart_send((uint8_t*)data, (uint16_t)len);
    printf("[SEND] data sent\r\n");

    /*
     * ESP8266???????????:
     *   "Recv xxx bytes\r\n"        ? ????
     *   "SEND OK\r\n"               ? ????
     *   "Recv xxx bytes\r\n+IPD,yyy:{...}\r\nCLOSED"  ? ????
     *
     * ??+IPD???Recv????,???????http_post??
     */
    t = HAL_GetTick();
    while((HAL_GetTick() - t) < 15000)
    {
        uint8_t *f = atk_mw8266d_uart_rx_get_frame_timeout(3000);
        if(f)
        {
            printf("[SEND] rx: %.80s\r\n", (char*)f);

            if(strstr((char*)f, "SEND OK") || strstr((char*)f, "Recv"))
            {
                /* ??????????+IPD???? */
                char *ipd = strstr((char*)f, "+IPD,");
                if(ipd)
                {
                    char *colon = strchr(ipd, ':');
                    if(colon)
                    {
                        char *resp_start = colon + 1;
                        int resp_len = strlen(resp_start);
                        if(resp_len > 0 && resp_len < (int)sizeof(cs_leftover) - 1)
                        {
                            memcpy(cs_leftover, resp_start, resp_len);
                            cs_leftover[resp_len] = '\0';
                            cs_leftover_len = resp_len;
                            printf("[SEND] saved %d bytes from merged frame\r\n", resp_len);
                        }
                    }
                }
                printf("[SEND] success\r\n");
                return true;
            }

            if(strstr((char*)f, "SEND FAIL"))
            {
                printf("[SEND] fail\r\n");
                return false;
            }
        }
        atk_mw8266d_uart_rx_restart();
    }

    printf("[SEND] timeout\r\n");
    return false;
}

/* ==================== SAFECLOSE ==================== */

static void safeclose(void)
{
    delay_ms(500);
    uart_drain(500);
    atk_mw8266d_send_at_cmd("AT+CIPCLOSE", "OK", 3000);
}

/* ==================== HTTP POST ==================== */

bool http_post(const char *path, const char *body, char *resp, int rsz)
{
    int rlen;
    uint32_t t;
    int total = 0;
    bool finished = false;
    bool got_ipd = false;

    printf("[HTTP] %s\r\n", path);

    if(!tcp_conn()){
        printf("[HTTP] tcp fail\r\n");
        return false;
    }

    rlen = snprintf(g_req, sizeof(g_req),
        "POST %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %u\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        path, SERVER_IP, SERVER_PORT,
        (unsigned)strlen(body), body);

    printf("[HTTP] req len=%d\r\n", rlen);

    if(!cipsend(g_req, rlen)){
        printf("[HTTP] cipsend fail\r\n");
        safeclose();
        return false;
    }

    printf("[HTTP] waiting response\r\n");
    memset(resp, 0, rsz);
    total = 0;
    finished = false;
    got_ipd = false;

    /* ??cipsend???+IPD??(Recv????) */
    if(cs_leftover_len > 0)
    {
        printf("[HTTP] using %d bytes from cipsend leftover\r\n", cs_leftover_len);

        /* ????????+IPD?? */
        char *ipd = strstr(cs_leftover, "+IPD,");
        if(ipd)
        {
            char *colon = strchr(ipd, ':');
            if(colon)
            {
                char *d = colon + 1;
                int l = strlen(d);
                if(l > 0 && total + l < rsz - 1)
                {
                    memcpy(resp + total, d, l);
                    total += l;
                    resp[total] = '\0';
                    got_ipd = true;
                }
            }
        }
        else
        {
            /* ??+IPD??,??????? */
            if(cs_leftover_len < rsz - 1)
            {
                memcpy(resp, cs_leftover, cs_leftover_len);
                total = cs_leftover_len;
                resp[total] = '\0';
                got_ipd = true;
            }
        }
        cs_leftover_len = 0;

        /* ??JSON????? */
        if(!finished)
        {
            char *js = strchr(resp, '{');
            if(js){
                int depth = 0;
                char *p = js;
                while(*p){
                    if(*p == '{') depth++;
                    if(*p == '}'){
                        depth--;
                        if(depth == 0){ finished = true; }
                    }
                    p++;
                }
            }
        }
    }

    /* ????? */
    t = HAL_GetTick();
    while(!finished && (HAL_GetTick()-t) < 20000)
    {
        uint8_t *f = atk_mw8266d_uart_rx_get_frame_timeout(3000);
        if(!f){
            if(got_ipd && total > 0) break;
            continue;
        }

        char *d = (char*)f;
        size_t l = strlen(d);

        /* ??+IPD */
        char *ipd = strstr(d, "+IPD,");
        if(ipd){
            got_ipd = true;
            char *colon = strchr(ipd, ':');
            if(colon){
                d = colon + 1;
                l = strlen(d);
            }
        }

        /* ????+IPD,??????(??CLOSED???) */
        if(!got_ipd){
            continue;
        }

        /* ?????? */
        if(strstr(d, "SEND OK") || strstr(d, "AT+CIPSEND") ||
           strstr(d, "Recv ") || (l==1 && d[0]=='>'))
        {
            continue;
        }

        /* CLOSED?? */
        if(l >= 6 && strncmp(d, "CLOSED", 6) == 0){
            printf("[HTTP] CLOSED\r\n");
            break;
        }

        /* ?????? */
        if(l > 0 && total + (int)l < rsz - 1){
            memcpy(resp + total, d, l);
            total += l;
            resp[total] = '\0';
        }

        /* ??JSON???? */
        {
            char *js = strchr(resp, '{');
            if(js){
                int depth = 0;
                char *p = js;
                while(*p){
                    if(*p == '{') depth++;
                    if(*p == '}'){
                        depth--;
                        if(depth == 0){ finished = true; break; }
                    }
                    p++;
                }
            }
        }
    }

    printf("[HTTP] got %d bytes, finished=%d\r\n", total, finished);

    if(total > 0){
        char *js = strchr(resp, '{');
        if(js) printf("[HTTP] json ok\r\n");
        else printf("[HTTP] no json found\r\n");
    }

    safeclose();
    return (total > 0);
}

/* ==================== ???API ==================== */

void ota_init(void)
{
    memset(&D, 0, sizeof(D));
    memset(g_msg, 0, sizeof(g_msg));
}

void srv_register(void)
{
    snprintf(g_body, sizeof(g_body),
        "{\"device_id\":\"%s\",\"device_model\":\"%s\"}", D.id, DEVICE_MODEL);

    printf("[REG] start\r\n");
    if(!http_post(API_REGISTER, g_body, g_resp, RESP_SZ)){
        printf("[REG] http fail\r\n");
        D.srv_ok = false;
        return;
    }

    char *js = strchr(g_resp, '{');
    if(!js){
        printf("[REG] no json\r\n");
        D.srv_ok = false;
        return;
    }

    int c = 0;
    jgi(js, "code", &c);
    printf("[REG] code=%d\r\n", c);
    if(c != 200){
        D.srv_ok = false;
        return;
    }

    jgs(js, "wifi_ssid", D.ssid, sizeof(D.ssid));
    jgs(js, "wifi_password", D.pass, sizeof(D.pass));
    D.srv_ok = true;
    printf("[REG] OK, wifi=%s\r\n", D.ssid);
}

void srv_heartbeat(void)
{
    snprintf(g_body, sizeof(g_body),
        "{\"device_id\":\"%s\",\"sys_ver\":\"%s\",\"model_ver\":\"%s\"}",
        D.id, D.sys_ver, D.md_ver);

    if(!http_post(API_HEARTBEAT, g_body, g_resp, RESP_SZ)){
        D.srv_ok = false;
        return;
    }

    char *js = strchr(g_resp, '{');
    if(!js){ D.srv_ok = false; return; }

    int c = 0;
    jgi(js, "code", &c);
    if(c != 200){ D.srv_ok = false; return; }

    D.srv_ok = true;

    char ns[64] = "";
    jgs(js, "wifi_ssid", ns, sizeof(ns));
    if(strlen(ns) > 0 && strcmp(ns, D.ssid) != 0){
        printf("[HB] wifi changed to %s\r\n", ns);
        strcpy(D.ssid, ns);
        jgs(js, "wifi_password", D.pass, sizeof(D.pass));
        wifi_connect();
    }
}

void check_fw(void)
{
    snprintf(g_body, sizeof(g_body),
        "{\"device_id\":\"%s\",\"current_ver\":\"%s\"}", D.id, D.sys_ver);

    if(!http_post(API_CHECK_FW, g_body, g_resp, RESP_SZ)){
        D.fw_upd = false;
        return;
    }

    char *js = strchr(g_resp, '{');
    if(!js){ D.fw_upd = false; return; }

    int c = 0;
    jgi(js, "code", &c);
    if(c != 200){ D.fw_upd = false; return; }

    D.fw_upd = false;
    jgb(js, "update_available", &D.fw_upd);

    if(D.fw_upd){
        jgs(js, "new_version", D.fw_ver, sizeof(D.fw_ver));
        int s = 0;
        jgi(js, "file_size", &s);
        D.fw_sz = (uint32_t)s;

        D.fw_patch = false;
        jgb(js, "patch_available", &D.fw_patch);
        if(D.fw_patch){
            jgi(js, "patch_size", &s);
            D.fw_patch_sz = (uint32_t)s;
        }
        printf("[FW] %s -> %s (%luB)\r\n", D.sys_ver, D.fw_ver, (unsigned long)D.fw_sz);
    } else {
        printf("[FW] up to date\r\n");
    }
}

void check_md(void)
{
    snprintf(g_body, sizeof(g_body),
        "{\"device_id\":\"%s\",\"current_ver\":\"%s\"}", D.id, D.md_ver);

    if(!http_post(API_CHECK_MD, g_body, g_resp, RESP_SZ)){
        D.md_upd = false;
        return;
    }

    char *js = strchr(g_resp, '{');
    if(!js){ D.md_upd = false; return; }

    int c = 0;
    jgi(js, "code", &c);
    if(c != 200){ D.md_upd = false; return; }

    D.md_upd = false;
    jgb(js, "update_available", &D.md_upd);

    if(D.md_upd){
        jgs(js, "new_version", D.md_ver_new, sizeof(D.md_ver_new));
        jgs(js, "model_name", D.md_name, sizeof(D.md_name));
        int s = 0;
        jgi(js, "file_size", &s);
        D.md_sz = (uint32_t)s;

        D.md_patch = false;
        jgb(js, "patch_available", &D.md_patch);
        if(D.md_patch){
            jgi(js, "patch_size", &s);
            D.md_patch_sz = (uint32_t)s;
        }
        printf("[MD] %s -> %s (%luB)\r\n", D.md_ver, D.md_ver_new, (unsigned long)D.md_sz);
    } else {
        printf("[MD] up to date\r\n");
    }
}
