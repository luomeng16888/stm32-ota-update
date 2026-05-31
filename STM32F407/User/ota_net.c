/**
 * ================================================================
 *  ota_net.c — 网络通信层 (v4.1 修订版)
 *
 *  v4.0 → v4.1:
 *    [FIX] dl_disconnect() 无条件执行 — 不再检查 g_dl_conn 标志,
 *          每次调用都会恢复中断模式 + 关闭 TCP, 防止 AT 指令通道被占用
 *    [FIX] dl_http_get() header 阶段捕获 CLOSED — 防止服务器主动断开时
 *          ESP8266 先发 CLOSED\r\n 到 UART, header 解析被污染导致失败
 *    [FIX] CLOSED peek 超时从 200ms 调整为 500ms
 *    [FIX] tcp_conn() 增加前置 exit_unvarnished 调用
 * ================================================================
 */
#include "ota.h"
#include "boot_cfg.h"
#include "./BSP/ATK_MW8266D/atk_mw8266d.h"
#include "./BSP/ATK_MW8266D/atk_mw8266d_uart.h"
#include "./SYSTEM/usart/usart.h"
#include "./SYSTEM/delay/delay.h"
#include "./BSP/LED/led.h"
#include "./MALLOC/malloc.h"
#include <stdlib.h>
#include <ctype.h>

/* ============================================================
 *  ESP8266 UART 实例 (默认引脚: USART3)
 * ============================================================ */
#ifndef ESP_UART_INST
#define ESP_UART_INST       USART3
#endif
#define ESP_IRQn            USART3_IRQn

/* ==================== 缓冲区大小 ==================== */
#define API_RESP_SZ         2048
#define REQ_BUF_SZ          512

/* ==================== 全局变量 ==================== */
Dev_t   D;
DlCtx_t g_dl;

static char *g_req_buf = NULL;
static char g_resp[API_RESP_SZ];
static bool g_dl_conn = false;

/* ==================== Hex 解码 ==================== */
uint8_t hv(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

/* ==================== JSON 解析 ==================== */
static char *json_body(char *resp)
{
    char *b = strstr(resp, "\r\n\r\n");
    return b ? b + 4 : resp;
}

static bool jstr(const char *j, const char *k, char *o, int ol)
{
    char s[64]; const char *p; int i;
    if (!j || !k || !o || ol <= 0) return false;
    snprintf(s, sizeof(s), "\"%s\":", k);
    p = strstr(j, s);
    if (!p) return false;
    p += strlen(s);
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '"') p++;
    i = 0;
    while (*p && *p != '"' && *p != ',' && *p != '}' &&
           *p != '\n' && *p != '\r' && i < ol - 1)
        o[i++] = *p++;
    o[i] = '\0';
    return i > 0;
}

static int jint(const char *j, const char *k)
{
    char v[32];
    if (jstr(j, k, v, sizeof(v))) return atoi(v);
    return 0;
}

static uint32_t juint(const char *j, const char *k)
{
    char v[32];
    if (jstr(j, k, v, sizeof(v))) return (uint32_t)atol(v);
    return 0;
}

static bool jbool(const char *j, const char *k)
{
    char v[16];
    if (!jstr(j, k, v, sizeof(v))) return false;
    return (v[0] == 't' || v[0] == 'T');
}

/* ==================== WiFi 连接 ==================== */
bool wifi_connect(void)
{
    uint8_t ret;
    printf("[WIFI] ssid=%s\r\n", D.ssid);

    ret = atk_mw8266d_init(115200);
    NVIC_ClearPendingIRQ(ESP_IRQn);
    NVIC_SetPriority(ESP_IRQn, 6);
    NVIC_EnableIRQ(ESP_IRQn);

    if (ret != 0) {
        printf("[WIFI] init fail, try restore...\r\n");
        atk_mw8266d_send_at_cmd("AT+RESTORE", "ready", 5000);
        delay_ms(3000);
        ret = atk_mw8266d_init(115200);
        NVIC_ClearPendingIRQ(ESP_IRQn);
        NVIC_SetPriority(ESP_IRQn, 6);
        NVIC_EnableIRQ(ESP_IRQn);
    }
    if (ret != 0) { printf("[WIFI] Init fail!\r\n"); return false; }
    printf("[WIFI] Init OK\r\n");

    atk_mw8266d_at_test();
    atk_mw8266d_set_mode(1);
    atk_mw8266d_sw_reset();
    delay_ms(2000);
    atk_mw8266d_at_test();
    atk_mw8266d_ate_config(0);

    printf("[WIFI] join AP...\r\n");
    ret = atk_mw8266d_join_ap(D.ssid, D.pass);
    printf("[WIFI] join=%d\r\n", ret);
    if (ret != 0) { printf("[WIFI] Join fail!\r\n"); return false; }
    printf("[WIFI] Connected!\r\n");
    return true;
}

/* ============================================================
 *  tcp_conn() — TCP 连接 + 透传模式
 *
 *  [v4.1] 增加前置 exit_unvarnished 调用:
 *         防止上次下载异常中断后模块仍处于透传模式,
 *         此时直接发送 AT 指令会失败, 需先退出透传
 * ============================================================ */
static bool tcp_conn(void)
{
    char cmd[64];
    uint8_t retry;

    printf("[TCP] Server: %s:%d\r\n", SERVER_IP, SERVER_PORT);

    /* [v4.1] 防御: 先退出透传模式 */
    atk_mw8266d_exit_unvarnished();
    delay_ms(500);
    atk_mw8266d_uart_rx_restart();
    delay_ms(200);

    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%d",
             SERVER_IP, SERVER_PORT);

    for (retry = 0; retry < 3; retry++) {
        printf("[TCP] attempt %d/3\r\n", retry + 1);
        atk_mw8266d_send_at_cmd("AT+CIPCLOSE", "OK", 500);
        delay_ms(500);

        uint32_t cr1_save = ESP_UART_INST->CR1;
        ESP_UART_INST->CR1 &= ~USART_CR1_RXNEIE;
        while (ESP_UART_INST->SR & USART_SR_RXNE)
            (void)ESP_UART_INST->DR;

        {
            char full_cmd[80];
            int i;
            snprintf(full_cmd, sizeof(full_cmd), "%s\r\n", cmd);
            for (i = 0; full_cmd[i]; i++) {
                while (!(ESP_UART_INST->SR & USART_SR_TXE));
                ESP_UART_INST->DR = (uint8_t)full_cmd[i];
            }
        }

        {
            char resp[256];
            int rlen = 0;
            uint32_t start = HAL_GetTick();
            bool got_connect = false, got_error = false;

            memset(resp, 0, sizeof(resp));
            while ((HAL_GetTick() - start) < 10000) {
                if (ESP_UART_INST->SR & USART_SR_RXNE) {
                    uint8_t ch = (uint8_t)(ESP_UART_INST->DR & 0xFF);
                    if (rlen < (int)sizeof(resp) - 1) {
                        resp[rlen++] = (char)ch;
                        resp[rlen] = '\0';
                    }
                    if (ch == '\n') {
                        while (rlen > 0 &&
                               (resp[rlen-1]=='\r' || resp[rlen-1]=='\n'))
                            resp[--rlen] = '\0';
                        if (rlen > 0)
                            printf("[TCP] rx: [%s]\r\n", resp);
                        if (strstr(resp, "CONNECT") ||
                            strstr(resp, "ALREADY CONNECTED"))
                            got_connect = true;
                        if (strstr(resp, "ERROR") || strstr(resp, "FAIL"))
                            got_error = true;
                        if (got_connect || got_error) break;
                        rlen = 0; resp[0] = '\0';
                    }
                }
            }

            ESP_UART_INST->CR1 = cr1_save;
            NVIC_ClearPendingIRQ(ESP_IRQn);
            NVIC_EnableIRQ(ESP_IRQn);

            if (got_connect) {
                printf("[TCP] connected!\r\n");
                delay_ms(500);
                if (atk_mw8266d_enter_unvarnished() == 0) {
                    printf("[TCP] transparent mode OK\r\n");
                    return true;
                }
                printf("[TCP] transparent mode fail\r\n");
            } else if (got_error) {
                printf("[TCP] server refused\r\n");
            } else {
                printf("[TCP] timeout\r\n");
            }
        }
        printf("[TCP] wait 3s...\r\n");
        delay_ms(3000);
    }
    printf("[TCP] all failed\r\n");
    return false;
}

/* ============================================================
 *  safeclose() — 安全关闭 + 断开 TCP
 * ============================================================ */
static void safeclose(void)
{
    delay_ms(500);
    atk_mw8266d_exit_unvarnished();
    delay_ms(1000);
    atk_mw8266d_send_at_cmd("AT+CIPCLOSE", "OK", 500);
    printf("[TCP] closed\r\n");
}

/* ============================================================
 *  http_recv_raw() — 直接读 UART DR 接收 HTTP 响应
 * ============================================================ */
static int http_recv_raw(char *buf, int maxlen, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    uint32_t now, last_ch = 0;
    int len = 0, hdr_end = -1, body_target = -1;
    bool have_cl = false;
    int ore_count = 0;

    while (1) {
        now = HAL_GetTick();
        if ((now - start) >= timeout_ms) break;

        if (ESP_UART_INST->SR & USART_SR_RXNE) {
            if (ESP_UART_INST->SR & USART_SR_ORE) ore_count++;
            uint8_t ch = (uint8_t)(ESP_UART_INST->DR & 0xFF);
            last_ch = now;
            if (len < maxlen - 1) { buf[len++] = (char)ch; buf[len] = '\0'; }

            if (hdr_end < 0 && len >= 4) {
                if (buf[len-4]=='\r' && buf[len-3]=='\n' &&
                    buf[len-2]=='\r' && buf[len-1]=='\n') {
                    hdr_end = len;
                    int i;
                    for (i = 0; i < len - 15; i++) {
                        if (buf[i]=='C' &&
                            strncmp(buf+i,"Content-Length:",15)==0) {
                            body_target = atoi(buf+i+15);
                            have_cl = (body_target > 0);
                            break;
                        }
                    }
                }
            }
        }
        if (have_cl && hdr_end >= 0 && (len - hdr_end) >= body_target) break;
        if (hdr_end >= 0 && !have_cl && last_ch > 0 && (now - last_ch) > 1000) break;
        if (have_cl && last_ch > 0 && (now - last_ch) > 10000) break;
    }

    printf("[RAW] %d bytes hdr=%d CL=%d body=%d ORE=%d\r\n",
           len, hdr_end, body_target,
           hdr_end >= 0 ? len - hdr_end : -1, ore_count);
    return len;
}

/* ============================================================
 *  http_post() — HTTP POST (新建连接, 用完即断)
 * ============================================================ */
bool http_post(const char *path, const char *body, char *resp, int rsz)
{
    int body_len = (int)strlen(body);
    int req_len, n;

    if (!tcp_conn()) { printf("[HTTP] tcp fail\r\n"); return false; }

    req_len = snprintf(g_req_buf, REQ_BUF_SZ,
        "POST %s HTTP/1.1\r\nHost: %s:%d\r\n"
        "Content-Type: application/json\r\nContent-Length: %d\r\n"
        "Connection: close\r\n\r\n%s",
        path, SERVER_IP, SERVER_PORT, body_len, body);

    printf("[HTTP] POST %s (%d bytes)\r\n", path, req_len);

    atk_mw8266d_uart_rx_restart();
    ESP_UART_INST->CR1 &= ~USART_CR1_RXNEIE;
    if (ESP_UART_INST->SR & USART_SR_ORE) (void)ESP_UART_INST->DR;
    while (ESP_UART_INST->SR & USART_SR_RXNE) (void)ESP_UART_INST->DR;

    atk_mw8266d_uart_send((uint8_t *)g_req_buf, (uint16_t)req_len);

    n = http_recv_raw(resp, rsz, 15000);

    ESP_UART_INST->CR1 |= USART_CR1_RXNEIE;
    NVIC_ClearPendingIRQ(ESP_IRQn);
    NVIC_EnableIRQ(ESP_IRQn);

    if (n <= 0) { printf("[HTTP] recv fail\r\n"); safeclose(); return false; }
    printf("[HTTP] got %d bytes\r\n", n);
    safeclose();
    return true;
}

/* ============================================================
 *  长连接下载相关
 * ============================================================ */

static int uart_read_byte(uint32_t timeout_ms)
{
    uint32_t t0 = HAL_GetTick();
    while ((HAL_GetTick() - t0) < timeout_ms) {
        if (ESP_UART_INST->SR & USART_SR_RXNE)
            return (int)(ESP_UART_INST->DR & 0xFF);
    }
    return -1;
}

bool dl_connect(void)
{
    if (g_dl_conn) return true;
    if (!tcp_conn()) { printf("[DL] tcp fail\r\n"); return false; }
    ESP_UART_INST->CR1 &= ~USART_CR1_RXNEIE;
    while (ESP_UART_INST->SR & USART_SR_RXNE) (void)ESP_UART_INST->DR;
    g_dl_conn = true;
    printf("[DL] connected (transparent)\r\n");
    return true;
}

/**
 * [v4.1] dl_disconnect() — 无条件恢复中断模式 + 关闭 TCP
 *
 * 删除了 if (!g_dl_conn) return 的提前返回逻辑。
 * 原因: 某些异常路径中, dl_http_get() 已经置 g_dl_conn=false,
 * 但 dl_to_sd() 仍会调 dl_disconnect() 做收尾, 此时因标志为 false 直接跳过,
 * 导致 ESP8266 卡在透传模式, 后续 AT 指令通道无法使用
 */
void dl_disconnect(void)
{
    /* 恢复 UART 中断 */
    ESP_UART_INST->CR1 |= USART_CR1_RXNEIE;
    NVIC_ClearPendingIRQ(ESP_IRQn);
    NVIC_EnableIRQ(ESP_IRQn);

    /* 退出透传模式 + 关闭 TCP */
    safeclose();
    g_dl_conn = false;
    printf("[DL] disconnected\r\n");
}

bool dl_is_connected(void) { return g_dl_conn; }

/**
 * dl_http_get() — 长连接 HTTP GET, 仅返回 body
 *
 * [v4.1] 新增 header 阶段捕获 CLOSED:
 *   防止服务器主动断开时 ESP8266 先发 CLOSED\r\n 到 UART,
 *   此时 header 解析被污染导致解析失败
 * [v4.1] CLOSED peek 超时从 200ms 调整为 500ms
 */
int dl_http_get(const char *path, uint8_t *buf, int buf_sz)
{
    char req[384];
    int req_len;
    char hdr[512];
    int hdr_len = 0;
    int content_length = -1;
    int status_code = 0;
    int ch;

    if (!g_dl_conn) return -1;

    /* 1. 发送 GET */
    req_len = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\nHost: %s:%d\r\n"
        "Connection: keep-alive\r\n\r\n",
        path, SERVER_IP, SERVER_PORT);

    atk_mw8266d_uart_send((uint8_t *)req, (uint16_t)req_len);

    /* 2. 读取响应头 + [v4.1] 检测 CLOSED */
    while (1) {
        ch = uart_read_byte(10000);
        if (ch < 0) {
            printf("[DL-GET] hdr timeout\r\n");
            g_dl_conn = false;
            return -1;
        }
        if (hdr_len < (int)sizeof(hdr) - 1) {
            hdr[hdr_len++] = (char)ch;
            hdr[hdr_len] = '\0';
        }

        /* [v4.1] 检测 CLOSED (服务器主动断开的标志) */
        if (hdr_len >= 6) {
            char *closed = strstr(hdr, "CLOSED\r\n");
            if (closed) {
                printf("[DL-GET] CLOSED detected in hdr\r\n");
                g_dl_conn = false;
                return -1;
            }
        }

        /* headers 结束 */
        if (hdr_len >= 4 &&
            hdr[hdr_len-4]=='\r' && hdr[hdr_len-3]=='\n' &&
            hdr[hdr_len-2]=='\r' && hdr[hdr_len-1]=='\n') {
            break;
        }
    }

    if (strncmp(hdr, "HTTP/1.1 ", 9) == 0)
        status_code = atoi(hdr + 9);
    {
        char *cl = strstr(hdr, "Content-Length:");
        if (!cl) cl = strstr(hdr, "content-length:");
        if (cl) content_length = atoi(cl + 15);
    }

    if (status_code != 200 || content_length <= 0) {
        printf("[DL-GET] err: status=%d CL=%d\r\n", status_code, content_length);
        g_dl_conn = false;
        return -1;
    }
    if (content_length > buf_sz) {
        printf("[DL-GET] body %d > buf %d\r\n", content_length, buf_sz);
        g_dl_conn = false;
        return -1;
    }

    /* 3. 读 body (逐字节) */
    {
        int n = 0;
        while (n < content_length) {
            ch = uart_read_byte(10000);
            if (ch < 0) {
                printf("[DL-GET] body timeout (%d/%d)\r\n", n, content_length);
                g_dl_conn = false;
                return n > 0 ? n : -1;
            }
            buf[n++] = (uint8_t)ch;
        }
    }

    /* 4. [v4.1] CLOSED peek: 200ms → 500ms */
    {
        char peek[16];
        int plen = 0;
        uint32_t t0 = HAL_GetTick();
        while (plen < 10 && (HAL_GetTick() - t0) < 500) {
            ch = uart_read_byte(50);
            if (ch < 0) break;
            if (plen < (int)sizeof(peek) - 1) peek[plen++] = (char)ch;
        }
        peek[plen] = '\0';
        if (plen > 0 && strstr(peek, "CLOSED")) {
            printf("[DL-GET] conn closed by server\r\n");
            g_dl_conn = false;
        }
    }

    return content_length;
}

/* ==================== 初始化 ==================== */
void ota_init(void)
{
    printf("[OTA] BUILD: %s %s\r\n", __DATE__, __TIME__);
    g_req_buf = (char *)mymalloc(SRAMIN, REQ_BUF_SZ);
    if (!g_req_buf) { printf("[OTA] malloc fail!\r\n"); while (1); }
    memset(&D, 0, sizeof(D));
    memset(&g_dl, 0, sizeof(g_dl));
    memset(g_resp, 0, sizeof(g_resp));
    g_dl_conn = false;
    printf("[OTA] Init OK (req=%dB resp=%dB)\r\n", REQ_BUF_SZ, API_RESP_SZ);
}

/* ==================== 注册 ==================== */
void srv_register(void)
{
    char body[384]; char *j; int code;
    snprintf(body, sizeof(body),
        "{\"device_id\":\"%s\",\"device_model\":\"%s\","
        "\"sys_version\":\"%s\",\"model_version\":\"%s\"}",
        D.id, DEVICE_MODEL, g_current_ver, D.md_ver);
    printf("[REG] ver=%s\r\n", g_current_ver);
    if (!http_post(API_REGISTER, body, g_resp, API_RESP_SZ)) {
        printf("[REG] http fail\r\n"); D.srv_ok = false; return;
    }
    j = json_body(g_resp);
    printf("[REG] body: [%.200s]\r\n", j);
    code = jint(j, "code");
    if (code == 0) { code = jint(g_resp, "code"); }
    printf("[REG] code=%d\r\n", code);
    if (code != 200) { D.srv_ok = false; return; }
    jstr(j, "wifi_ssid", D.ssid, sizeof(D.ssid));
    jstr(j, "wifi_password", D.pass, sizeof(D.pass));
    D.srv_ok = true;
    printf("[REG] OK\r\n");
}

/* ==================== 心跳 ==================== */
void srv_heartbeat(void)
{
    char body[256]; char *j; int code;
    snprintf(body, sizeof(body),
        "{\"device_id\":\"%s\",\"sys_ver\":\"%s\",\"model_ver\":\"%s\"}",
        D.id, g_current_ver, D.md_ver);
    printf("[HB] ver=%s\r\n", g_current_ver);
    if (!http_post(API_HEARTBEAT, body, g_resp, API_RESP_SZ)) {
        printf("[HB] http fail\r\n"); D.srv_ok = false; return;
    }
    j = json_body(g_resp); code = jint(j, "code");
    if (code != 200) { D.srv_ok = false; return; }
    D.srv_ok = true;
    { char ns[64]=""; jstr(j,"wifi_ssid",ns,sizeof(ns));
      if (strlen(ns)>0 && strcmp(ns,D.ssid)!=0) {
        printf("[HB] wifi changed\r\n"); strcpy(D.ssid,ns);
        jstr(j,"wifi_password",D.pass,sizeof(D.pass));
        wifi_connect(); srv_register(); }}
    printf("[HB] OK\r\n");
}

/* ==================== 固件检查 ==================== */
void check_fw(void)
{
    char body[256]; char *j;
    D.fw_upd=false; D.fw_patch=false; D.fw_sz=0;
    snprintf(body, sizeof(body),
        "{\"device_id\":\"%s\",\"current_ver\":\"%s\"}", D.id, g_current_ver);
    printf("[FW] check: ver=%s\r\n", g_current_ver);
    if (!http_post(API_CHECK_FW, body, g_resp, API_RESP_SZ)) return;
    j = json_body(g_resp);
    printf("[FW] body: [%.200s]\r\n", j);
    if (!jbool(j, "update_available")) { printf("[FW] up to date\r\n"); return; }
    D.fw_upd=true; jstr(j,"new_version",D.fw_ver,sizeof(D.fw_ver));
    D.fw_sz=juint(j,"file_size");
    D.fw_patch=jbool(j,"patch_available");
    if (D.fw_patch) D.fw_patch_sz=juint(j,"patch_size");
    printf("[FW] %s -> %s (%luB, patch=%d)\r\n",
           g_current_ver, D.fw_ver, (unsigned long)D.fw_sz, D.fw_patch);
}

/* ==================== 模型检查 ==================== */
void check_md(void)
{
    char body[256]; char *j;
    D.md_upd=false; D.md_patch=false; D.md_sz=0;
    snprintf(body, sizeof(body),
        "{\"device_id\":\"%s\",\"current_ver\":\"%s\"}", D.id, D.md_ver);
    printf("[MD] check: ver=%s\r\n", D.md_ver);
    if (!http_post(API_CHECK_MD, body, g_resp, API_RESP_SZ)) return;
    j = json_body(g_resp);
    printf("[MD] body: [%.200s]\r\n", j);
    if (!jbool(j, "update_available")) { printf("[MD] up to date\r\n"); return; }
    D.md_upd=true; jstr(j,"new_version",D.md_ver_new,sizeof(D.md_ver_new));
    jstr(j,"model_name",D.md_name,sizeof(D.md_name));
    D.md_sz=juint(j,"file_size");
    D.md_patch=jbool(j,"patch_available");
    if (D.md_patch) D.md_patch_sz=juint(j,"patch_size");
    printf("[MD] %s -> %s (%s, %luB, patch=%d)\r\n",
           D.md_ver, D.md_ver_new, D.md_name,
           (unsigned long)D.md_sz, D.md_patch);
}
