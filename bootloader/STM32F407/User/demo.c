#include "demo.h"
#include "ota.h"
#include "./BSP/ATK_MW8266D/atk_mw8266d.h"
#include "./BSP/ATK_MW8266D/atk_mw8266d_uart.h"
#include "./SYSTEM/usart/usart.h"
#include "./SYSTEM/delay/delay.h"
#include "./BSP/LED/led.h"
#include "./BSP/KEY/key.h"
#include "./BSP/LCD/lcd.h"
#include "./BSP/SDIO/sdio_sdcard.h"
#include "./FATFS/source/ff.h"
#include "./MALLOC/malloc.h"

extern uint32_t g_point_color;
extern uint32_t g_back_color;

static FATFS *gf = NULL;
static State_t g_st = ST_BOOT;
static bool g_ref = true, g_ok = false;

static void lcd_s(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t sz, const char *s)
{
    lcd_show_string(x, y, w, h, sz, (char *)s, (uint16_t)g_point_color);
}

void demo_main(void)
{
    uint32_t hb_t=0;
    uint8_t key;
    bool wifi_ok=false;

    atk_mw8266d_uart_init(115200);
    usart_init(115200);
    printf("\r\n========== OTA v21.0 ==========\r\n");
    my_mem_init(SRAMIN);

    g_point_color = BLACK;
    g_back_color = WHITE;

    /* ===== SD???? ===== */
    lcd_clear(WHITE);
    lcd_s(10,30,300,16,16,"STM32 OTA v21.0");
    lcd_s(10,60,300,16,16,"Init SD card...");

    printf("[SD] Init\r\n");
    {uint8_t r=0; while(sd_init()){delay_ms(500);LED0_TOGGLE();if(++r>=5)break;}}

    gf=(FATFS*)mymalloc(SRAMIN,sizeof(FATFS));
    if(gf){
        FRESULT res=f_mount(gf,"0:",1);
        if(res==FR_NO_FILESYSTEM){
            lcd_s(10,90,300,16,16,"Formatting SD...");
            f_mkfs("0:",0,0,FF_MAX_SS);
            res=f_mount(gf,"0:",1);
        }
        printf("[SD] Mount %s\r\n",(res==FR_OK)?"OK":"FAIL");
    }

    f_mkdir(SD_MODELS);
    f_mkdir(SD_FW);
    f_mkdir(SD_TMP);
    f_mkdir("0:/firmware/tmp");

    /* ===== ???? ===== */
    ota_init();
    strcpy(D.id, DEVICE_ID);
    strcpy(D.ssid, WIFI_SSID);
    strcpy(D.pass, WIFI_PASS);
    strcpy(D.sys_ver, SYS_VERSION);
    load_md_ver(D.md_ver, 16);
    printf("[DEV] %s sys=%s md=%s\r\n", D.id, D.sys_ver, D.md_ver);

    /* ===== WiFi?? ===== */
    lcd_clear(WHITE);
    lcd_s(10,30,300,16,16,"STM32 OTA v21.0");
    lcd_s(10,60,300,16,16,"Connecting WiFi...");
    {char buf[48]; snprintf(buf,48,"SSID: %s",D.ssid); lcd_s(10,85,300,16,16,buf);}

    wifi_ok = wifi_connect();

    if(wifi_ok){
        lcd_s(10,120,300,16,16,"WiFi OK!");
        lcd_s(10,145,300,16,16,"Registering device...");
        srv_register();
        if(D.srv_ok)
            lcd_s(10,170,300,16,16,"Server OK!");
        else
            lcd_s(10,170,300,16,16,"Server fail (retry later)");
    } else {
        lcd_s(10,120,300,16,16,"WiFi FAILED!");
        lcd_s(10,145,300,16,16,"Check SSID & password");
    }

    delay_ms(1000);
    printf("================================\r\n\r\n");

    g_st = ST_MAIN;
    g_ref = true;
    hb_t = HAL_GetTick();

    /* ===== ??? ===== */
    while(1)
    {
        /* ????(60?) */
        if(D.srv_ok && (HAL_GetTick()-hb_t >= 60000)){
            hb_t = HAL_GetTick();
            srv_heartbeat();
        }

        /* WiFi??????(30?) */
        if(!wifi_ok && (HAL_GetTick()-hb_t >= 30000)){
            hb_t = HAL_GetTick();
            lcd_clear(WHITE);
            lcd_s(10,80,300,16,16,"Reconnecting WiFi...");
            wifi_ok = wifi_connect();
            if(wifi_ok){
                lcd_s(10,110,300,16,16,"Reconnected!");
                srv_register();
                delay_ms(500);
            }
            g_ref = true;
        }

        /* ???? */
        key = key_scan(0);

        switch(g_st)
        {
        /* ---------- ??? ---------- */
        case ST_MAIN:
            if(g_ref){lcd_main_page();g_ref=false;}
            if(key==KEY0_PRES){
                printf("[KEY0] wifi=%d srv=%d\r\n",wifi_ok,D.srv_ok);
                if(!wifi_ok){
                    lcd_clear(WHITE);
                    lcd_s(10,80,300,16,16,"Connecting WiFi...");
                    wifi_ok = wifi_connect();
                    if(wifi_ok) srv_register();
                }
                if(wifi_ok && D.srv_ok){
                    g_st = ST_CHECKING;
                    g_ref = true;
                } else {
                    lcd_clear(WHITE);
                    lcd_s(10,80,300,16,16,"WiFi or Server fail!");
                    lcd_s(10,110,300,16,16,"Press any key...");
                    while(key_scan(0)==0) delay_ms(50);
                    g_ref = true;
                }
            } else if(key==KEY1_PRES){
                g_st = ST_RUNNING;
                g_ref = true;
            }
            break;

        /* ---------- ???? ---------- */
        case ST_CHECKING:
            lcd_clear(WHITE);
            lcd_s(10,70,300,16,16,"Checking firmware...");
            lcd_s(10,95,300,16,16,"Please wait...");
            check_fw();
            lcd_s(10,130,300,16,16,"Checking model...");
            lcd_s(10,155,300,16,16,"Please wait...");
            check_md();
            g_st = ST_UPDATE_SEL;
            g_ref = true;
            break;

        /* ---------- ???? ---------- */
        case ST_UPDATE_SEL:
            if(g_ref){lcd_update_sel();g_ref=false;}
            if(key==KEY0_PRES && D.fw_upd){
                g_st = ST_FW_SEL;
                g_ref = true;
            } else if(key==KEY1_PRES && D.md_upd){
                g_st = ST_MD_SEL;
                g_ref = true;
            } else if(key==KEY2_PRES){
                g_st = ST_MAIN;
                g_ref = true;
            } else if(key!=0 && !D.fw_upd && !D.md_upd){
                /* ??????,????? */
                g_st = ST_MAIN;
                g_ref = true;
            }
            break;

        /* ---------- ?????? ---------- */
        case ST_FW_SEL:
            if(g_ref){lcd_fw_sel();g_ref=false;}
            if(key==KEY0_PRES){
                printf("[KEY0] firmware full update\r\n");
                lcd_clear(WHITE);
                lcd_s(10,80,300,16,16,"Starting firmware download...");
                delay_ms(500);
                g_ok = fw_full();
                g_st = ST_RESULT;
                g_ref = true;
            } else if(key==KEY1_PRES){
                if(D.fw_patch){
                    printf("[KEY1] firmware diff update\r\n");
                    lcd_clear(WHITE);
                    lcd_s(10,80,300,16,16,"Starting patch download...");
                    delay_ms(500);
                    g_ok = fw_diff();
                    g_st = ST_RESULT;
                    g_ref = true;
                } else {
                    lcd_clear(WHITE);
                    lcd_s(10,80,300,16,16,"No patch available!");
                    lcd_s(10,110,300,16,16,"Use KEY0 for full update");
                    lcd_s(10,140,300,16,16,"Press any key...");
                    while(key_scan(0)==0) delay_ms(50);
                    g_ref = true;
                }
            } else if(key==KEY2_PRES){
                g_st = ST_UPDATE_SEL;
                g_ref = true;
            }
            break;

        /* ---------- ???? ---------- */
        case ST_MD_SEL:
            if(g_ref){lcd_md_sel();g_ref=false;}
            if(key==KEY0_PRES){
                printf("[KEY0] model full update\r\n");
                lcd_clear(WHITE);
                lcd_s(10,80,300,16,16,"Starting model download...");
                delay_ms(500);
                g_ok = md_full();
                g_st = ST_RESULT;
                g_ref = true;
            } else if(key==KEY1_PRES){
                if(D.md_patch){
                    printf("[KEY1] model diff update\r\n");
                    lcd_clear(WHITE);
                    lcd_s(10,80,300,16,16,"Starting patch download...");
                    delay_ms(500);
                    g_ok = md_diff();
                    g_st = ST_RESULT;
                    g_ref = true;
                } else {
                    lcd_clear(WHITE);
                    lcd_s(10,80,300,16,16,"No patch available!");
                    lcd_s(10,110,300,16,16,"Use KEY0 for full update");
                    lcd_s(10,140,300,16,16,"Press any key...");
                    while(key_scan(0)==0) delay_ms(50);
                    g_ref = true;
                }
            } else if(key==KEY2_PRES){
                g_st = ST_RUNNING;
                g_ref = true;
            }
            break;

        /* ---------- ???? ---------- */
        case ST_RESULT:
            if(g_ref){lcd_result(g_ok, g_msg);g_ref=false;}
            if(key!=0){
                g_st = ST_MAIN;
                g_ref = true;
            }
            break;

        /* ---------- ???? ---------- */
        case ST_RUNNING:
            if(g_ref){lcd_run_page();g_ref=false;}
            /* TODO: ????????? */
            if(key==KEY0_PRES){
                g_st = ST_MAIN;
                g_ref = true;
            }
            break;

        default:
            g_st = ST_MAIN;
            g_ref = true;
            break;
        }

        delay_ms(50);
    }
}

void demo_run(void){demo_main();}
