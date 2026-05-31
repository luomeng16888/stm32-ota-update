#include "ota.h"
#include "./BSP/LCD/lcd.h"
#include "./BSP/SDIO/sdio_sdcard.h"
#include "./BSP/LED/led.h"
#include "./BSP/ATK_MW8266D/atk_mw8266d.h"
#include "./BSP/ATK_MW8266D/atk_mw8266d_uart.h"
#include "./FATFS/source/ff.h"
#include "./MALLOC/malloc.h"
#include "./SYSTEM/delay/delay.h"
#include "./SYSTEM/usart/usart.h"

extern uint32_t g_point_color;
extern uint32_t g_back_color;

static void lcd_str(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t sz, const char *s)
{
    lcd_show_string(x, y, w, h, sz, (char *)s, (uint16_t)g_point_color);
}

static uint8_t hv(char c)
{
    if(c>='0'&&c<='9') return c-'0';
    if(c>='a'&&c<='f') return c-'a'+10;
    if(c>='A'&&c<='F') return c-'A'+10;
    return 0;
}

static int jgi_l(const char *j, const char *k, int *v)
{
    char s[64]; const char *p,*q; char *e;
    if(!j||!k||!v) return 0;
    snprintf(s,64,"\"%s\":",k); p=strstr(j,s); if(!p) return 0;
    q=p+strlen(s);
    while(*q==' '||*q=='\t'||*q=='\r'||*q=='\n') q++;
    *v=(int)strtol(q,&e,10); return e!=q;
}

/* ==================== LCD ==================== */

void lcd_dl(const char *lbl, uint32_t cur, uint32_t tot, uint32_t speed)
{
    char buf[64];
    int pct = (tot>0) ? (int)(cur*100/tot) : 0;
    int eta = (speed>0 && cur<tot) ? (int)((tot-cur)/speed) : 0;
    int bar_w = 200, bar_h = 16, bar_x = 10, bar_y = 100;
    int fill_w = (pct * bar_w) / 100;

    lcd_clear(WHITE);
    lcd_str(10,10,300,16,16,"DOWNLOADING...");
    lcd_str(10,35,300,16,16,lbl);

    if(tot >= 1024*1024)
        snprintf(buf,64,"%.1fMB / %.1fMB",
                 cur/(1024.0f*1024.0f), tot/(1024.0f*1024.0f));
    else if(tot >= 1024)
        snprintf(buf,64,"%luKB / %luKB",
                 (unsigned long)(cur/1024), (unsigned long)(tot/1024));
    else
        snprintf(buf,64,"%luB / %luB",
                 (unsigned long)cur, (unsigned long)tot);
    lcd_str(10,60,300,16,16,buf);

    snprintf(buf,64,"Progress: %d%%", pct);
    lcd_str(10,80,300,16,16,buf);

    lcd_draw_rectangle(bar_x, bar_y, bar_x+bar_w, bar_y+bar_h, BLACK);
    if(fill_w > 0)
        lcd_fill(bar_x+1, bar_y+1, bar_x+fill_w-1, bar_y+bar_h-1, BLUE);

    if(speed >= 1024)
        snprintf(buf,64,"Speed: %.1f KB/s", speed/1024.0f);
    else
        snprintf(buf,64,"Speed: %lu B/s", (unsigned long)speed);
    lcd_str(10,125,300,16,16,buf);

    if(eta > 60)
        snprintf(buf,64,"ETA: %dm %ds", eta/60, eta%60);
    else if(eta > 0)
        snprintf(buf,64,"ETA: %ds", eta);
    else
        snprintf(buf,64,"ETA: ...");
    lcd_str(10,145,300,16,16,buf);
}

void lcd_main_page(void)
{
    char b[48]; lcd_clear(WHITE);
    lcd_str(10,5,300,16,16,"STM32 OTA Update System");
    lcd_str(10,25,300,16,16,"-----------------------------");
    snprintf(b,48,"ID: %s",D.id);
    lcd_str(10,45,300,16,16,b);
    snprintf(b,48,"WiFi: %s",D.ssid);
    lcd_str(10,70,300,16,16,b);
    snprintf(b,48,"Server: %s",D.srv_ok?"Connected":"Disconnected");
    lcd_str(10,90,300,16,16,b);
    lcd_str(10,115,300,16,16,"-----------------------------");
    snprintf(b,48,"Sys:   v%s",D.sys_ver);
    lcd_str(10,135,300,16,16,b);
    snprintf(b,48,"Model: v%s",D.md_ver);
    lcd_str(10,155,300,16,16,b);
    lcd_str(10,180,300,16,16,"-----------------------------");
    lcd_str(10,200,300,16,16,"KEY0:Update  KEY1:Run");
}

void lcd_update_sel(void)
{
    char b[48]; lcd_clear(WHITE);
    lcd_str(10,5,300,16,16,"=== UPDATE MODE ===");
    lcd_str(10,30,300,16,16,"[SYSTEM FIRMWARE]");
    snprintf(b,48,"Current: v%s",D.sys_ver);
    lcd_str(10,50,300,16,16,b);
    if(D.fw_upd){
        snprintf(b,48,"Latest:  v%s",D.fw_ver);
        lcd_str(10,70,300,16,16,b);
        lcd_str(10,90,300,16,16,"Status: UPDATE AVAILABLE");
    } else lcd_str(10,70,300,16,16,"Status: UP TO DATE");
    lcd_str(10,115,300,16,16,"[MODEL WEIGHT]");
    snprintf(b,48,"Current: v%s",D.md_ver);
    lcd_str(10,135,300,16,16,b);
    if(D.md_upd){
        snprintf(b,48,"Latest:  v%s",D.md_ver_new);
        lcd_str(10,155,300,16,16,b);
        lcd_str(10,175,300,16,16,"Status: UPDATE AVAILABLE");
    } else lcd_str(10,155,300,16,16,"Status: UP TO DATE");
    lcd_str(10,200,300,16,16,"KEY0:FW  KEY1:Model  KEY2:Back");
}

void lcd_fw_sel(void)
{
    char b[48]; lcd_clear(WHITE);
    lcd_str(10,5,300,16,16,"=== SYSTEM UPDATE ===");
    snprintf(b,48,"v%s -> v%s",D.sys_ver,D.fw_ver);
    lcd_str(10,30,300,16,16,b);
    snprintf(b,48,"Size: %luKB",(unsigned long)(D.fw_sz/1024));
    lcd_str(10,55,300,16,16,b);
    snprintf(b,48,"KEY0: Full (%luKB)",(unsigned long)(D.fw_sz/1024));
    lcd_str(10,90,300,16,16,b);
    if(D.fw_patch){
        snprintf(b,48,"KEY1: Diff (%luKB)",(unsigned long)(D.fw_patch_sz/1024));
        lcd_str(10,115,300,16,16,b);
    } else lcd_str(10,115,300,16,16,"KEY1: Diff (N/A)");
    lcd_str(10,145,300,16,16,"KEY2: Cancel");
}

void lcd_md_sel(void)
{
    char b[48]; lcd_clear(WHITE);
    lcd_str(10,5,300,16,16,"=== MODEL UPDATE ===");
    snprintf(b,48,"v%s -> v%s",D.md_ver,D.md_ver_new);
    lcd_str(10,30,300,16,16,b);
    if(strlen(D.md_name)>0){
        snprintf(b,48,"Name: %s",D.md_name);
        lcd_str(10,55,300,16,16,b);
    }
    snprintf(b,48,"Size: %luKB",(unsigned long)(D.md_sz/1024));
    lcd_str(10,80,300,16,16,b);
    snprintf(b,48,"KEY0: Full (%luKB)",(unsigned long)(D.md_sz/1024));
    lcd_str(10,110,300,16,16,b);
    if(D.md_patch){
        snprintf(b,48,"KEY1: Diff (%luKB)",(unsigned long)(D.md_patch_sz/1024));
        lcd_str(10,135,300,16,16,b);
    } else lcd_str(10,135,300,16,16,"KEY1: Diff (N/A)");
    lcd_str(10,165,300,16,16,"KEY2: Run Current Model");
}

void lcd_result(bool ok, const char *msg)
{
    lcd_clear(WHITE);
    lcd_str(10,30,300,16,16,ok?"UPDATE COMPLETE!":"UPDATE FAILED!");
    lcd_str(10,55,300,16,16,ok?"Status: SUCCESS":"Status: ERROR");
    lcd_str(10,85,300,16,16,msg);
    lcd_str(10,180,300,16,16,"Press any key");
}

void lcd_run_page(void)
{
    char b[48]; lcd_clear(WHITE);
    lcd_str(10,5,300,16,16,"=== RUNNING MODEL ===");
    snprintf(b,48,"Model: v%s",D.md_ver);
    lcd_str(10,30,300,16,16,b);
    lcd_str(10,60,300,16,16,"Inference running...");
    lcd_str(10,180,300,16,16,"KEY0: Stop & Return");
}

/* ==================== ???? ==================== */

void load_md_ver(char *v, size_t ml)
{
    FIL f; UINT b;
    if(f_open(&f,SD_CUR_MODEL,FA_READ)==FR_OK){
        f_read(&f,v,ml-1,&b); v[b]='\0';
        char *p=strchr(v,'\n');if(p)*p='\0';
        p=strchr(v,'\r');if(p)*p='\0';
        f_close(&f);
    } else strcpy(v,"0.0");
}

/* ==================== ?????? ==================== */

bool dl_file_tm(const char *api, const char *type, const char *from_v,
                const char *to_v, const char *path, uint32_t exp_sz, const char *lbl)
{
    static char body_buf[256];
    static uint8_t wb[CHUNK_SZ];
    char req_local[400];
    FIL fil;
    int off = 0;
    uint32_t start_time, last_lcd = 0;
    char cmd[128];
    int retry_count = 0;
    bool done = false;

    printf("[TM] %s v%s %luB chunk=%d api=%s\r\n",
           type, to_v, (unsigned long)exp_sz, CHUNK_SZ, api);
    f_unlink(path);

    if(f_open(&fil, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK){
        printf("[TM] open fail\r\n");
        return false;
    }

    lcd_dl(lbl, 0, exp_sz, 0);
    start_time = HAL_GetTick();

retry:
    /* ===== Step 1: TCP?? ===== */
    atk_mw8266d_send_at_cmd("AT+CIPCLOSE", "OK", 2000);
    delay_ms(200);

    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%d",
             SERVER_IP, SERVER_PORT);
    printf("[TM] %s\r\n", cmd);
    if(atk_mw8266d_send_at_cmd(cmd, "OK", 15000) != 0){
        printf("[TM] tcp fail\r\n");
        f_close(&fil); f_unlink(path);
        return false;
    }
    printf("[TM] tcp ok\r\n");
    delay_ms(500);

    /* ===== Step 2: ?????? ===== */
    if(atk_mw8266d_send_at_cmd("AT+CIPMODE=1", "OK", 2000) != 0){
        printf("[TM] cipmode fail\r\n");
        atk_mw8266d_send_at_cmd("AT+CIPCLOSE", "OK", 2000);
        f_close(&fil); f_unlink(path);
        return false;
    }
    printf("[TM] cipmode ok\r\n");
    delay_ms(200);

    /* ===== Step 3: AT+CIPSEND(?? > ???) ===== */
    printf("[TM] AT+CIPSEND\r\n");
    atk_mw8266d_uart_rx_restart();
    atk_mw8266d_uart_printf("AT+CIPSEND\r\n");

    {
        bool got_prompt = false;
        uint32_t t = HAL_GetTick();
        while(HAL_GetTick() - t < 3000){
            uint8_t *f = atk_mw8266d_uart_rx_get_frame_timeout(500);
            if(f){
                printf("[TM] CIPSEND: [%s]\r\n", (char*)f);
                if(strstr((char*)f, ">") || strstr((char*)f, "OK")){
                    got_prompt = true;
                    printf("[TM] got prompt\r\n");
                    break;
                }
                if(strstr((char*)f, "ERROR")) break;
            }
        }
        if(!got_prompt){
            printf("[TM] no prompt\r\n");
            goto tm_retry;
        }
    }

    delay_ms(300);
    atk_mw8266d_uart_rx_restart();
    delay_ms(100);
    atk_mw8266d_uart_rx_restart();
    printf("[TM] transparent ready\r\n");

    /* ===== ???? ===== */
    while(off < (int)exp_sz)
    {
        int blen, rlen, resp_len;
        bool found_json;
        uint32_t t;
        char *js;
        uint8_t *f;

        /* ??API?????????? */
        if(!strcmp(api, API_DL_PATCH))
            blen = snprintf(body_buf, sizeof(body_buf),
                "{\"type\":\"%s\",\"from_version\":\"%s\",\"to_version\":\"%s\","
                "\"offset\":%d,\"length\":%d}",
                type, from_v, to_v, off, CHUNK_SZ);
        else
            blen = snprintf(body_buf, sizeof(body_buf),
                "{\"device_id\":\"%s\",\"type\":\"%s\",\"version\":\"%s\","
                "\"offset\":%d,\"length\":%d}",
                D.id, type, to_v, off, CHUNK_SZ);

        rlen = snprintf(req_local, sizeof(req_local),
            "POST %s HTTP/1.1\r\n"
            "Host: %s:%d\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %d\r\n"
            "Connection: keep-alive\r\n"
            "\r\n"
            "%s",
            api, SERVER_IP, SERVER_PORT, blen, body_buf);

        printf("[TM] send %d\r\n", rlen);
        atk_mw8266d_uart_send((uint8_t*)req_local, (uint16_t)rlen);

        /* ????HTTP?? */
        resp_len = 0;
        g_resp[0] = '\0';
        found_json = false;
        t = HAL_GetTick();

        for(;;)
        {
            f = atk_mw8266d_uart_rx_get_frame_timeout(5000);
            if(!f){
                if(resp_len > 0){
                    printf("[TM] timeout, retry read\r\n");
                    f = atk_mw8266d_uart_rx_get_frame_timeout(2000);
                    if(!f){
                        printf("[TM] second timeout, acc=%d\r\n", resp_len);
                        break;
                    }
                } else {
                    printf("[TM] timeout, no data\r\n");
                    break;
                }
            }

            {
                char *d = (char*)f;
                int flen = strlen(d);

                while(flen > 0 && *d == '>'){ d++; flen--; }
                if(flen <= 0){ atk_mw8266d_uart_rx_restart(); continue; }

                printf("[TM] frame[%d]\r\n", flen);

                if(strstr(d, "CLOSED")){
                    printf("[TM] CLOSED\r\n");
                    goto tm_retry;
                }

                if(resp_len + flen < RESP_SZ - 1){
                    memcpy(g_resp + resp_len, d, flen);
                    resp_len += flen;
                    g_resp[resp_len] = '\0';
                }

                js = strchr(g_resp, '{');
                if(js){
                    int depth = 0;
                    char *p = js;
                    while(*p){
                        if(*p == '{') depth++;
                        if(*p == '}'){
                            depth--;
                            if(depth == 0){ found_json = true; break; }
                        }
                        p++;
                    }
                }

                if(found_json){
                    printf("[TM] json found, len=%d\r\n", resp_len);
                    break;
                }
            }
            atk_mw8266d_uart_rx_restart();
        }

        if(!found_json){
            printf("[TM] no json acc=%d\r\n", resp_len);
            if(resp_len > 0 && resp_len < 300)
                printf("[TM] raw: %.200s\r\n", g_resp);
            goto tm_retry;
        }

        /* ??JSON */
        js = strchr(g_resp, '{');
        {
            int code = 0, cs = 0;
            char *hd, *he;
            int hl, wr = 0;
            UINT bw;
            int wp = 0, i;

            jgi_l(js, "code", &code);
            jgi_l(js, "chunk_size", &cs);

            if(code != 200 || !cs){
                printf("[TM] code=%d cs=%d\r\n", code, cs);
                goto tm_retry;
            }

            hd = strstr(js, "\"data\":\"");
            if(!hd){ goto tm_retry; }
            hd += 8;
            he = strchr(hd, '"');
            if(!he){ goto tm_retry; }
            *he = '\0';

            hl = (int)strlen(hd);
            for(i = 0; i + 1 < hl; i += 2){
                wb[wp++] = (hv(hd[i]) << 4) | hv(hd[i + 1]);
                if(wp >= CHUNK_SZ){ f_write(&fil, wb, wp, &bw); wp = 0; }
                wr++;
            }
            if(wp > 0) f_write(&fil, wb, wp, &bw);

            off += wr;
            retry_count = 0;

            {
                uint32_t now = HAL_GetTick();
                if(now - last_lcd >= 500 || off >= (int)exp_sz){
                    uint32_t elapsed = now - start_time;
                    uint32_t speed = elapsed > 0 ?
                        (uint32_t)((uint64_t)off * 1000 / elapsed) : 0;
                    lcd_dl(lbl, (uint32_t)off, exp_sz, speed);
                    last_lcd = now;
                }
            }

            printf("[TM] %d/%lu\r\n", off, (unsigned long)exp_sz);
            if(wr < CHUNK_SZ) goto tm_done;
        }
        continue;

    tm_retry:
        retry_count++;
        if(retry_count > 3){
            printf("[TM] max retries\r\n");
            break;
        }
        printf("[TM] retry %d/3\r\n", retry_count);

        delay_ms(1000);
        atk_mw8266d_uart_send((uint8_t*)"+++", 3);
        delay_ms(1000);
        atk_mw8266d_send_at_cmd("AT+CIPCLOSE", "OK", 3000);
        atk_mw8266d_send_at_cmd("AT+CIPMODE=0", "OK", 2000);

        delay_ms(2000);
        goto retry;
    }

tm_done:
    done = (off >= (int)exp_sz);

    delay_ms(500);
    atk_mw8266d_uart_send((uint8_t*)"+++", 3);
    delay_ms(1000);
    atk_mw8266d_send_at_cmd("AT+CIPCLOSE", "OK", 3000);
    atk_mw8266d_send_at_cmd("AT+CIPMODE=0", "OK", 2000);

    f_close(&fil);

    if(done){
        uint32_t total_ms = HAL_GetTick() - start_time;
        uint32_t speed = total_ms > 0 ?
            (uint32_t)((uint64_t)off * 1000 / total_ms) : 0;
        printf("[TM] done %dB %lus %luB/s\r\n",
               off, (unsigned long)(total_ms / 1000), (unsigned long)speed);
        return true;
    }

    printf("[TM] fail %d/%lu\r\n", off, (unsigned long)exp_sz);
    f_unlink(path);
    return false;
}

/* ==================== BSPatch ==================== */

static int64_t offtin(const uint8_t *buf)
{
    int64_t y=buf[7]&0x7F;
    for(int i=6;i>=0;i--) y=y*256+buf[i];
    if(buf[7]&0x80) y=-y;
    return y;
}

bool bspatch_apply(const char *old_p, const char *patch_p, const char *new_p)
{
    FIL pf,of,nf;
    UINT br,bw;
    uint8_t h[32];
    int64_t cl,dl,ns,op=0,np=0;
    DWORD co,do2,eo;
    uint32_t start = HAL_GetTick();
    FILINFO fno;

    printf("[PATCH] %s + %s\r\n",old_p,patch_p);

    /* ??????????? */
    if(f_stat(patch_p, &fno) != FR_OK){
        printf("[PATCH] patch file not found\r\n");
        return false;
    }
    printf("[PATCH] patch size: %lu\r\n", (unsigned long)fno.fsize);

    if(f_stat(old_p, &fno) != FR_OK){
        printf("[PATCH] old file not found\r\n");
        return false;
    }
    printf("[PATCH] old size: %lu\r\n", (unsigned long)fno.fsize);

    if(f_open(&pf,patch_p,FA_READ)!=FR_OK){
        printf("[PATCH] open patch fail\r\n");
        return false;
    }
    if(f_open(&of,old_p,FA_READ)!=FR_OK){
        printf("[PATCH] open old fail\r\n");
        f_close(&pf);return false;
    }
    if(f_open(&nf,new_p,FA_WRITE|FA_CREATE_ALWAYS)!=FR_OK){
        printf("[PATCH] open new fail\r\n");
        f_close(&pf);f_close(&of);return false;
    }

    /* ??????? */
    if(f_read(&pf,h,32,&br)!=FR_OK){
        printf("[PATCH] read header fail\r\n");
        goto fail;
    }
    if(br!=32){
        printf("[PATCH] header short: %d bytes\r\n", br);
        goto fail;
    }
    if(memcmp(h,"BSDIFF40",8)!=0){
        printf("[PATCH] bad magic: %02x%02x%02x%02x%02x%02x%02x%02x\r\n",
               h[0],h[1],h[2],h[3],h[4],h[5],h[6],h[7]);
        goto fail;
    }

    cl=offtin(h+8); dl=offtin(h+16); ns=offtin(h+24);
    printf("[PATCH] cl=%lld dl=%lld ns=%lld\r\n",
           (long long)cl, (long long)dl, (long long)ns);

    if(ns<=0||cl<=0){
        printf("[PATCH] invalid sizes\r\n");
        goto fail;
    }

	co=32; do2=32+(DWORD)cl; eo=32+(DWORD)cl+(DWORD)dl;

    printf("[PATCH] co=%lu do2=%lu eo=%lu\r\n",
           (unsigned long)co, (unsigned long)do2, (unsigned long)eo);

    /* ?????????????? */
    {
        DWORD patch_size;
        f_stat(patch_p, &fno);
        patch_size = (DWORD)fno.fsize;
        if(eo > patch_size){
            printf("[PATCH] ERROR: eo(%lu) > patch_size(%lu)\r\n",
                   (unsigned long)eo, (unsigned long)patch_size);
            goto fail;
        }
    }

    while(np<ns){
        uint8_t cb[24]; int64_t al,cp2,sa;
        f_lseek(&pf,co);
        if(f_read(&pf,cb,24,&br)!=FR_OK||br!=24){
            printf("[PATCH] read control fail at co=%lu br=%d\r\n",
                   (unsigned long)co, br);
            goto fail;
        }
        co+=24;

        al=offtin(cb); cp2=offtin(cb+8); sa=offtin(cb+16);
        if(al<0||cp2<0){
            printf("[PATCH] invalid control: al=%lld cp2=%lld\r\n",
                   (long long)al, (long long)cp2);
            goto fail;
        }

        /* ????:?????? */
        {int64_t rm=al;
        while(rm>0){
            uint8_t db[512],ob[512];
            int ck=(rm>512)?512:(int)rm;
            UINT obr;
            f_lseek(&pf,do2);
            if(f_read(&pf,db,ck,&br)!=FR_OK||(int)br!=ck){
                printf("[PATCH] read diff fail at do2=%lu br=%d ck=%d\r\n",
                       (unsigned long)do2, br, ck);
                goto fail;
            }
            do2+=ck;
            f_lseek(&of,(DWORD)op);
            f_read(&of,ob,ck,&obr);
            for(int i=0;i<ck;i++) db[i]+=(i<(int)obr)?ob[i]:0;
            if(f_write(&nf,db,ck,&bw)!=FR_OK){
                printf("[PATCH] write fail\r\n");
                goto fail;
            }
            op+=ck; np+=ck; rm-=ck;
        }}

        /* ????:???? */
        {int64_t rm=cp2;
        while(rm>0){
            uint8_t buf2[512];
            int ck=(rm>512)?512:(int)rm;
            f_lseek(&pf,eo);
            if(f_read(&pf,buf2,ck,&br)!=FR_OK||(int)br!=ck){
                printf("[PATCH] read extra fail at eo=%lu br=%d ck=%d\r\n",
                       (unsigned long)eo, br, ck);
                goto fail;
            }
            eo+=ck;
            if(f_write(&nf,buf2,ck,&bw)!=FR_OK){
                printf("[PATCH] write extra fail\r\n");
                goto fail;
            }
            np+=ck; rm-=ck;
        }}

        op+=sa;
    }

    f_close(&pf); f_close(&of); f_close(&nf);
    printf("[PATCH] OK %ldB %lums\r\n",
           (long)ns,(unsigned long)(HAL_GetTick()-start));
    return true;

fail:
    f_close(&pf); f_close(&of); f_close(&nf); f_unlink(new_p);
    printf("[PATCH] FAIL\r\n");
    return false;
}


/* ==================== ???? ==================== */

bool fw_full(void)
{
    static char sp[64],vd[64],fp[64];
    f_mkdir(SD_FW); f_mkdir("0:/firmware/tmp");
    snprintf(sp,64,"0:/firmware/tmp/fw.bin");
    snprintf(vd,64,"%s/%s",SD_FW,D.fw_ver);
    snprintf(fp,64,"%s/firmware.bin",vd);

    if(!dl_file_tm(API_DOWNLOAD,"firmware",D.sys_ver,D.fw_ver,sp,D.fw_sz,"System FW")){
        strcpy(g_msg,"Download failed"); return false;
    }

    f_mkdir(vd); f_unlink(fp);
    if(f_rename(sp,fp)!=FR_OK){strcpy(g_msg,"Move failed");return false;}

    FIL f;
    if(f_open(&f,SD_CUR_FW,FA_CREATE_ALWAYS|FA_WRITE)==FR_OK){
        UINT bw; f_write(&f,D.fw_ver,strlen(D.fw_ver),&bw); f_close(&f);
    }

    snprintf(g_msg,64,"FW v%s saved",D.fw_ver);
    return true;
}

bool fw_diff(void)
{
    static char pp[64],op2[80],np2[64],vd[64],fp[64];
    FRESULT fr;
    FIL f;

    f_mkdir(SD_FW); f_mkdir("0:/firmware/tmp");
    snprintf(pp,64,"0:/firmware/tmp/patch.bsdiff");
    snprintf(op2,80,"%s/%s/firmware.bin",SD_FW,D.sys_ver);
    snprintf(np2,64,"0:/firmware/tmp/fw_new.bin");
    snprintf(vd,64,"%s/%s",SD_FW,D.fw_ver);
    snprintf(fp,64,"%s/firmware.bin",vd);

    printf("[FW-DIFF] old: %s\r\n", op2);
    printf("[FW-DIFF] new: %s\r\n", fp);

    if(!dl_file_tm(API_DL_PATCH,"firmware",D.sys_ver,D.fw_ver,pp,D.fw_patch_sz,"FW Patch")){
        strcpy(g_msg,"Patch download failed"); return false;
    }

    if(f_open(&f,op2,FA_READ)!=FR_OK){
        printf("[FW-DIFF] ERROR: old firmware not found\r\n");
        strcpy(g_msg,"Old firmware not found"); return false;
    }
    f_close(&f);

    lcd_clear(WHITE);
    lcd_str(10,60,300,16,16,"Applying patch...");
    lcd_str(10,85,300,16,16,"Please wait...");

    if(!bspatch_apply(op2,pp,np2)){
        strcpy(g_msg,"BSPatch failed"); f_unlink(pp); return false;
    }

    f_mkdir(vd); f_unlink(fp);
    fr = f_rename(np2,fp);
    printf("[FW-DIFF] rename: %d\r\n", fr);
    if(fr != FR_OK){ strcpy(g_msg,"Rename failed"); return false; }

    if(f_open(&f,SD_CUR_FW,FA_CREATE_ALWAYS|FA_WRITE)==FR_OK){
        UINT bw; f_write(&f,D.fw_ver,strlen(D.fw_ver),&bw); f_close(&f);
    }

    f_unlink(pp);

    snprintf(g_msg,64,"FW v%s diff OK",D.fw_ver);
    printf("[FW-DIFF] SUCCESS: %s\r\n", g_msg);
    return true;
}


/* ==================== ???? ==================== */

bool md_full(void)
{
    static char sp[64],vd[64],fp[64],vt[64];
    f_mkdir(SD_MODELS); f_mkdir(SD_TMP);
    snprintf(sp,64,"%s/model.bin",SD_TMP);
    snprintf(vd,64,"%s/%s",SD_MODELS,D.md_ver_new);
    snprintf(fp,64,"%s/model.bin",vd);
    snprintf(vt,64,"%s/version.txt",vd);

    if(!dl_file_tm(API_DOWNLOAD,"model",D.md_ver,D.md_ver_new,sp,D.md_sz,"Model")){
        strcpy(g_msg,"Download failed"); return false;
    }

    f_mkdir(vd); f_unlink(fp);
    if(f_rename(sp,fp)!=FR_OK){strcpy(g_msg,"Move failed");return false;}

    FIL f;
    if(f_open(&f,vt,FA_CREATE_ALWAYS|FA_WRITE)==FR_OK){
        UINT bw; f_write(&f,D.md_ver_new,strlen(D.md_ver_new),&bw); f_close(&f);
    }
    if(f_open(&f,SD_CUR_MODEL,FA_CREATE_ALWAYS|FA_WRITE)==FR_OK){
        UINT bw; f_write(&f,D.md_ver_new,strlen(D.md_ver_new),&bw); f_close(&f);
    }

    strcpy(D.md_ver,D.md_ver_new);
    snprintf(g_msg,64,"Model v%s ready",D.md_ver_new);
    return true;
}

bool md_diff(void)
{
    static char pp[64],op2[80],np2[64],vd[64],fp[64],vt[64];
    FRESULT fr;
    FIL f;

    f_mkdir(SD_MODELS); f_mkdir(SD_TMP);
    snprintf(pp,64,"%s/patch.bsdiff",SD_TMP);
    snprintf(op2,80,"%s/%s/model.bin",SD_MODELS,D.md_ver);
    snprintf(np2,64,"%s/model_new.bin",SD_TMP);
    snprintf(vd,64,"%s/%s",SD_MODELS,D.md_ver_new);
    snprintf(fp,64,"%s/model.bin",vd);
    snprintf(vt,64,"%s/version.txt",vd);

    printf("[MD-DIFF] old model: %s\r\n", op2);
    printf("[MD-DIFF] patch:     %s\r\n", pp);
    printf("[MD-DIFF] new model: %s\r\n", fp);

    if(!dl_file_tm(API_DL_PATCH,"model",D.md_ver,D.md_ver_new,pp,D.md_patch_sz,"Model Patch")){
        strcpy(g_msg,"Patch download failed"); return false;
    }

    /* ??????????? */
    if(f_open(&f,op2,FA_READ)!=FR_OK){
        printf("[MD-DIFF] ERROR: old model not found: %s\r\n", op2);
        strcpy(g_msg,"Old model not found"); return false;
    }
    f_close(&f);

    lcd_clear(WHITE);
    lcd_str(10,60,300,16,16,"Applying patch...");
    lcd_str(10,85,300,16,16,"Please wait...");

    if(!bspatch_apply(op2,pp,np2)){
        printf("[MD-DIFF] bspatch failed\r\n");
        strcpy(g_msg,"BSPatch failed"); f_unlink(pp); return false;
    }

    f_mkdir(vd);
    f_unlink(fp);
    fr = f_rename(np2,fp);
    printf("[MD-DIFF] rename %s -> %s: %d\r\n", np2, fp, fr);
    if(fr != FR_OK){
        strcpy(g_msg,"Rename failed"); f_unlink(pp); return false;
    }

    /* ????? */
    if(f_open(&f,vt,FA_CREATE_ALWAYS|FA_WRITE)==FR_OK){
        UINT bw; f_write(&f,D.md_ver_new,strlen(D.md_ver_new),&bw); f_close(&f);
        printf("[MD-DIFF] wrote %s: %s\r\n", vt, D.md_ver_new);
    }
    if(f_open(&f,SD_CUR_MODEL,FA_CREATE_ALWAYS|FA_WRITE)==FR_OK){
        UINT bw; f_write(&f,D.md_ver_new,strlen(D.md_ver_new),&bw); f_close(&f);
        printf("[MD-DIFF] wrote %s: %s\r\n", SD_CUR_MODEL, D.md_ver_new);
    }

    /* ???????? */
    printf("[MD-DIFF] version: %s -> %s\r\n", D.md_ver, D.md_ver_new);
    strcpy(D.md_ver, D.md_ver_new);

    f_unlink(pp);

    snprintf(g_msg,64,"Model v%s diff OK", D.md_ver_new);
    printf("[MD-DIFF] SUCCESS: %s\r\n", g_msg);
    return true;
}

