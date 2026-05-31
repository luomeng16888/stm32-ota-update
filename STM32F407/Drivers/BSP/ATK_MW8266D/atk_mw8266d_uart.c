/**
 ****************************************************************************************************
 * @file        atk_mw8266d_uart.c
 * @author      ??????(ALIENTEK)
 * @version     V1.1 (Fixed > prompt detection)
 * @date        2026-04-22
 * @brief       ATK-MW8266D??UART??????
 ****************************************************************************************************
 */

#include "./BSP/ATK_MW8266D/atk_mw8266d_uart.h"
#include "./SYSTEM/usart/usart.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static UART_HandleTypeDef g_uart_handle;                    /* ATK-MW8266D UART */
static struct
{
    uint8_t buf[ATK_MW8266D_UART_RX_BUF_SIZE];              /* ????? */
    struct
    {
        uint16_t len    : 15;                               /* ?????,sta[14:0] */
        uint16_t finsh  : 1;                                /* ???????,sta[15] */
    } sta;                                                  /* ????? */
} g_uart_rx_frame = {0};                                    /* ATK-MW8266D UART?????????? */
static uint8_t g_uart_tx_buf[ATK_MW8266D_UART_TX_BUF_SIZE]; /* ATK-MW8266D UART???? */

/**
 * @brief       ATK-MW8266D UART printf
 * @param       fmt: ??????
 * @retval      ?
 */
void atk_mw8266d_uart_printf(char *fmt, ...)
{
    va_list ap;
    uint16_t len;

    va_start(ap, fmt);
    vsprintf((char *)g_uart_tx_buf, fmt, ap);
    va_end(ap);

    len = strlen((const char *)g_uart_tx_buf);
    HAL_UART_Transmit(&g_uart_handle, g_uart_tx_buf, len, HAL_MAX_DELAY);
}

/**
 * @brief       ATK-MW8266D UART * @retval      ?
 */
void atk_mw8266d_uart_rx_restart(void)
{
    g_uart_rx_frame.sta.len     = 0;
    g_uart_rx_frame.sta.finsh   = 0;
}

/**
 * @brief       ??ATK-MW8266D UART????????
 * @????????
 * @param       ?
param       ?
 * @retval      NULL: ????????
 *              ??: ????????
 */
uint8_t *atk_mw8266d_uart_rx_get_frame(void)
{
    if (g_uart_rx_frame.sta.finsh == 1)
    {
        g_uart_rx_frame.buf[g_uart_rx_frame.sta.len] = '\0';
        return g_uart_rx_frame.buf;
    }
    else
    {
        return NULL;
    }
}

/**
 * @brief       ??ATK-MW8266D UART???????????
 * @param       ?
 * @retval      0   : ????????
 *              ??: ???????????
 */
uint16_t atk_mw8266d_uart_rx_get_frame_len(void)
{
    if (g_uart_rx_frame.sta.finsh == 1)
    {
        return g_uart_rx_frame.sta.len;
    }
    else
    {
        return 0;
    }
}

/**
 * @brief       ATK-MW8266D UART???
 * @param       baudrate: UART?????
 * @retval      ?
 */
void atk_mw8266d_uart_init(uint32_t baudrate)
{
    g_uart_handle.Instance          = ATK_MW8266D_UART_INTERFACE;   /* ATK-MW8266D UART */
    g_uart_handle.Init.BaudRate     = baudrate;                     /* ??? */
    g_uart_handle.Init.WordLength   = UART_WORDLENGTH_8B;           /* ??? */
    g_uart_handle.Init.StopBits     = UART_STOPBITS_1;              /* ??? */
    g_uart_handle.Init.Parity       = UART_PARITY_NONE;             /* ??? */
    g_uart_handle.Init.Mode         = UART_MODE_TX_RX;              /* ???? */
    g_uart_handle.Init.HwFlowCtl    = UART_HWCONTROL_NONE;          /* ????? */
    g_uart_handle.Init.OverSampling = UART_OVERSAMPLING_16;         /* ??? */
    HAL_UART_Init(&g_uart_handle);                                  /* ??ATK-MW8266D UART
                                                                     * HAL_UART_Init()?????HAL_UART_MspInit()
                                                                     * ????????usart.c?
                                                                     */
}

/**
 * @brief       ATK-MW8266D UART??????
 * @param       ?
 * @retval      ?
 *
 * @attention   ????:
 *   ???????IDLE???????,?ESP8266?'>'?????
 *   ??????(?"CLOSED\r\n"),???????,IDLE????????
 *
 *   ????RXNE?????????????:
 *   1. ??'\n'???????'\r' -> AT?????
 *   2. ??'>' -> CIPSEND???,????
 *   3. ???? -> ????
 *   4. IDLE????????
 */
void ATK_MW8266D_UART_IRQHandler(void)
{
    uint8_t tmp;

    if (__HAL_UART_GET_FLAG(&g_uart_handle, UART_FLAG_ORE) != RESET)        /* UART???????? */
    {
        __HAL_UART_CLEAR_OREFLAG(&g_uart_handle);                           /* ???????????? */
        (void)g_uart_handle.Instance->SR;                                   /* ??SR???,??DR??? */
        (void)g_uart_handle.Instance->DR;
    }

    if (__HAL_UART_GET_FLAG(&g_uart_handle, UART_FLAG_RXNE) != RESET)       /* UART???? */
    {
        HAL_UART_Receive(&g_uart_handle, &tmp, 1, HAL_MAX_DELAY);           /* UART???? */

        /* ??????????,???(????) */
        if (g_uart_rx_frame.sta.finsh == 1)
        {
            g_uart_rx_frame.sta.len = 0;
            g_uart_rx_frame.sta.finsh = 0;
        }

        if (g_uart_rx_frame.sta.len < (ATK_MW8266D_UART_RX_BUF_SIZE - 1))   /* ??UART???????? */
        {
            g_uart_rx_frame.buf[g_uart_rx_frame.sta.len] = tmp;             /* ??????????? */
            g_uart_rx_frame.sta.len++;                                      /* ?????????? */
        }
        else                                                                /* UART?????? */
        {
            g_uart_rx_frame.sta.len = 0;                                    /* ????????? */
            g_uart_rx_frame.buf[g_uart_rx_frame.sta.len] = tmp;             /* ??????????? */
            g_uart_rx_frame.sta.len++;                                      /* ?????????? */
        }

        /* ======== ?????(????) ======== */

        if (tmp == '>')                                                     /* ?? '>':CIPSEND??? */
        {
            g_uart_rx_frame.sta.finsh = 1;                                  /* ??????? */
        }
        else if (g_uart_rx_frame.sta.len >= 2)                              /* ????? \r\n ?? */
        {
            if ((g_uart_rx_frame.buf[g_uart_rx_frame.sta.len - 2] == '\r') &&
                (g_uart_rx_frame.buf[g_uart_rx_frame.sta.len - 1] == '\n'))
            {
                g_uart_rx_frame.sta.finsh = 1;                              /* \r\n ??,????? */
            }
        }
    }

    if (__HAL_UART_GET_FLAG(&g_uart_handle, UART_FLAG_IDLE) != RESET)       /* UART?????? */
    {
        if (g_uart_rx_frame.sta.len > 0)                                    /* ??????????? */
        {
            g_uart_rx_frame.sta.finsh = 1;                                  /* ??????? */
        }

        __HAL_UART_CLEAR_IDLEFLAG(&g_uart_handle);                          /* ??UART?????? */
    }
}

/**
 * @brief       ?ATK-MW8266D??????????
 * @param       data: ????
 * @param       len:  ????
 * @retval      ?
 */
void atk_mw8266d_uart_send(uint8_t *data, uint16_t len)
{
    HAL_UART_Transmit(&g_uart_handle, data, len, 2000);
}
