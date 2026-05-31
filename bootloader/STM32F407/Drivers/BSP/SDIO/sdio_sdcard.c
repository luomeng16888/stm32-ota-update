/**
 ****************************************************************************************************
 * @file        sdio_sdcard.c
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2021-11-05
 * @brief       SD卡 驱动代码
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:正点原子 探索者 F407开发板
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 *
 * 修改说明
 * V1.0 20211105
 * 第一次发布
 *
 ****************************************************************************************************
 */

#include "string.h"
#include "./SYSTEM/usart/usart.h"
#include "./BSP/SDIO/sdio_sdcard.h"
#include "./SYSTEM/delay/delay.h"

SD_HandleTypeDef g_sdcard_handler;            /* SD卡句柄 */
HAL_SD_CardInfoTypeDef g_sd_card_info_handle; /* SD卡信息结构体 */

/**
 * @brief       初始化SD卡
 * @param       无
 * @retval      返回值:0 初始化正确；其他值，初始化错误
 */
uint8_t sd_init(void)
{
    uint8_t SD_Error;

    printf("[SD] ======================================\r\n");
    printf("[SD] 开始初始化 SD 卡...\r\n");
    printf("[SD] ======================================\r\n");

    /* 初始化时的时钟不能大于400KHZ */
    g_sdcard_handler.Instance = SDIO;
    g_sdcard_handler.Init.ClockEdge = SDIO_CLOCK_EDGE_RISING;                       /* 上升沿 */
    g_sdcard_handler.Init.ClockBypass = SDIO_CLOCK_BYPASS_DISABLE;                  /* 不使用bypass模式，直接用HCLK进行分频得到SDIO_CK */
    g_sdcard_handler.Init.ClockPowerSave = SDIO_CLOCK_POWER_SAVE_DISABLE;           /* 空闲时不关闭时钟电源 */
    g_sdcard_handler.Init.BusWide = SDIO_BUS_WIDE_1B;                               /* 1位数据线 */
    g_sdcard_handler.Init.HardwareFlowControl = SDIO_HARDWARE_FLOW_CONTROL_DISABLE; /* 关闭硬件流控 */
    g_sdcard_handler.Init.ClockDiv = SDIO_TRANSF_CLK_DIV;                           /* SD传输时钟频率最大25MHZ */

    printf("[SD] 初始化时钟: 400kHz (1位总线)\r\n");

    SD_Error = HAL_SD_Init(&g_sdcard_handler);
    if (SD_Error != HAL_OK)
    {
        printf("[SD] 初始化失败! Error: 0x%02X\r\n", SD_Error);
        return 1;
    }

    printf("[SD] 初始化成功!\r\n");

    HAL_SD_GetCardInfo(&g_sdcard_handler, &g_sd_card_info_handle);                  /* 获取SD卡信息 */

    printf("[SD] 卡信息:\r\n");
    printf("[SD]   卡型: %lu\r\n", g_sd_card_info_handle.CardType);
    printf("[SD]   卡容量: %lu MB\r\n", g_sd_card_info_handle.BlockNbr / 2048);
    printf("[SD]   块大小: %lu bytes\r\n", g_sd_card_info_handle.BlockSize);

    SD_Error = HAL_SD_ConfigWideBusOperation(&g_sdcard_handler, SDIO_BUS_WIDE_4B);  /* 使能4bit宽总线模式 */
    if (SD_Error != HAL_OK)
    {
        printf("[SD] 4位总线配置失败! Error: 0x%02X\r\n", SD_Error);
        printf("[SD] 将使用1位总线继续...\r\n");
        return 2;
    }

    printf("[SD] 4位总线模式启用成功!\r\n");
    printf("[SD] ======================================\r\n\r\n");

    return 0;
}

/**
 * @brief       SDIO底层驱动，时钟使能，引脚配置
                此函数会被HAL_SD_Init()调用
 * @param       hsd:SD卡句柄
 * @retval      无
 */
void HAL_SD_MspInit(SD_HandleTypeDef *hsd)
{
    GPIO_InitTypeDef gpio_init_struct;

    __HAL_RCC_SDIO_CLK_ENABLE();    /* 使能SDIO时钟 */
    SD_D0_GPIO_CLK_ENABLE();        /* D0引脚IO时钟使能 */
    SD_D1_GPIO_CLK_ENABLE();        /* D1引脚IO时钟使能 */
    SD_D2_GPIO_CLK_ENABLE();        /* D2引脚IO时钟使能 */
    SD_D3_GPIO_CLK_ENABLE();        /* D3引脚IO时钟使能 */
    SD_CLK_GPIO_CLK_ENABLE();       /* CLK引脚IO时钟使能 */
    SD_CMD_GPIO_CLK_ENABLE();       /* CMD引脚IO时钟使能 */

    gpio_init_struct.Pin = SD_D0_GPIO_PIN;              /* SD_D0引脚模式设置 */
    gpio_init_struct.Mode = GPIO_MODE_AF_PP;            /* 推挽复用 */
    gpio_init_struct.Pull = GPIO_PULLUP;                /* 上拉 */
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;      /* 高速 */
    gpio_init_struct.Alternate = GPIO_AF12_SDIO;        /* 复用为SDIO */
    HAL_GPIO_Init(SD_D0_GPIO_PORT, &gpio_init_struct);  /* 初始化 */

    gpio_init_struct.Pin = SD_D1_GPIO_PIN;              /* SD_D1引脚模式设置 */
    HAL_GPIO_Init(SD_D1_GPIO_PORT, &gpio_init_struct);  /* 初始化 */

    gpio_init_struct.Pin = SD_D2_GPIO_PIN;              /* SD_D2引脚模式设置 */
    HAL_GPIO_Init(SD_D2_GPIO_PORT, &gpio_init_struct);  /* 初始化 */

    gpio_init_struct.Pin = SD_D3_GPIO_PIN;              /* SD_D3引脚模式设置 */
    HAL_GPIO_Init(SD_D3_GPIO_PORT, &gpio_init_struct);  /* 初始化 */

    gpio_init_struct.Pin = SD_CLK_GPIO_PIN;             /* SD_CLK引脚模式设置 */
    HAL_GPIO_Init(SD_CLK_GPIO_PORT, &gpio_init_struct); /* 初始化 */

    gpio_init_struct.Pin = SD_CMD_GPIO_PIN;             /* SD_CMD引脚模式设置 */
    HAL_GPIO_Init(SD_CMD_GPIO_PORT, &gpio_init_struct); /* 初始化 */
}

/**
 * @brief       获取卡信息函数
 * @param       cardinfo:SD卡信息句柄
 * @retval      返回值:读取卡信息状态值
 */
uint8_t get_sd_card_info(HAL_SD_CardInfoTypeDef *cardinfo)
{
    uint8_t sta;

    sta = HAL_SD_GetCardInfo(&g_sdcard_handler, cardinfo);

    return sta;
}

/**
 * @brief       判断SD卡是否可以传输(读写)数据
 * @param       无
 * @retval      返回值:SD_TRANSFER_OK      传输完成，可以继续下一次传输
                       SD_TRANSFER_BUSY SD 卡正忙，不可以进行下一次传输
 */
uint8_t get_sd_card_state(void)
{
    return ((HAL_SD_GetCardState(&g_sdcard_handler) == HAL_SD_CARD_TRANSFER) ? SD_TRANSFER_OK : SD_TRANSFER_BUSY);
}

/**
 * @brief       读SD卡(fatfs/usb调用)
 * @param       pbuf  : 数据缓存区
 * @param       saddr : 扇区地址
 * @param       cnt   : 扇区个数
 * @retval      0, 正常;  其他, 错误代码(详见SD_Error定义);
 */
uint8_t sd_read_disk(uint8_t *pbuf, uint32_t saddr, uint32_t cnt)
{
    uint8_t sta = HAL_OK;
    uint32_t timeout = SD_TIMEOUT;
    long long lsector = saddr;

    __disable_irq();                                                                       /* 关闭总中断(POLLING模式,严禁中断打断SDIO读写操作!!!) */
    sta = HAL_SD_ReadBlocks(&g_sdcard_handler, (uint8_t *)pbuf, lsector, cnt, SD_TIMEOUT); /* 多个sector的读操作 */

    /* 等待SD卡读完 */
    while (get_sd_card_state() != SD_TRANSFER_OK)
    {
        if (timeout-- == 0)
        {
            sta = SD_TRANSFER_BUSY;
        }
    }
    __enable_irq(); /* 开启总中断 */

    return sta;
}

/**
 * @brief       写SD卡(fatfs/usb调用)
 * @param       pbuf  : 数据缓存区
 * @param       saddr : 扇区地址
 * @param       cnt   : 扇区个数
 * @retval      0, 正常;  其他, 错误代码(详见SD_Error定义);
 */
uint8_t sd_write_disk(uint8_t *pbuf, uint32_t saddr, uint32_t cnt)
{
    uint8_t sta = HAL_OK;
    uint32_t timeout = SD_TIMEOUT;
    long long lsector = saddr;

    __disable_irq();                                                                        /* 关闭总中断(POLLING模式,严禁中断打断SDIO读写操作!!!) */
    sta = HAL_SD_WriteBlocks(&g_sdcard_handler, (uint8_t *)pbuf, lsector, cnt, SD_TIMEOUT); /* 多个sector的写操作 */

    /* 等待SD卡写完 */
    while (get_sd_card_state() != SD_TRANSFER_OK)
    {
        if (timeout-- == 0)
        {
            sta = SD_TRANSFER_BUSY;
        }
    }
    __enable_irq();     /* 开启总中断 */

    return sta;
}

/**
 * @brief       检测SD卡插入状态
 * @retval      1: SD卡已插入, 0: 未插入
 */
uint8_t sd_card_inserted(void)
{
    /* 正点原子探索者F407: PC13检测SD卡插入 */
    GPIO_InitTypeDef GPIO_InitStructure;

    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitStructure.Pin = GPIO_PIN_13;
    GPIO_InitStructure.Mode = GPIO_MODE_INPUT;
    GPIO_InitStructure.Pull = GPIO_PULLUP;  /* 上拉 */
    GPIO_InitStructure.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStructure);

    /* 读取PC13电平 */
    uint8_t pin_state = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13);

    printf("[SD] PC13电平: %s\r\n", pin_state ? "HIGH (未插入)" : "LOW (已插入)");

    /* 低电平表示SD卡插入 */
    return (pin_state == GPIO_PIN_RESET);
}
