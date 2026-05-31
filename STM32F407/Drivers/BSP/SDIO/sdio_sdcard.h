/**
 ****************************************************************************************************
 * @file        sdio_sdcard.h
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

#ifndef __SDIO_SDCARD_H
#define __SDIO_SDCARD_H

#include "./SYSTEM/sys/sys.h"


/******************************************************************************************/
/* SDIO的信号线: SD_D0 ~ SD_D3/SD_CLK/SD_CMD 引脚 定义 
 * 如果你使用了其他引脚做SDIO的信号线,修改这里写定义即可适配.
 */

#define SD_D0_GPIO_PORT                GPIOC
#define SD_D0_GPIO_PIN                 GPIO_PIN_8
#define SD_D0_GPIO_CLK_ENABLE()        do{ __HAL_RCC_GPIOC_CLK_ENABLE(); }while(0)    /* 所在IO口时钟使能 */

#define SD_D1_GPIO_PORT                GPIOC
#define SD_D1_GPIO_PIN                 GPIO_PIN_9
#define SD_D1_GPIO_CLK_ENABLE()        do{ __HAL_RCC_GPIOC_CLK_ENABLE(); }while(0)    /* 所在IO口时钟使能 */

#define SD_D2_GPIO_PORT                GPIOC
#define SD_D2_GPIO_PIN                 GPIO_PIN_10
#define SD_D2_GPIO_CLK_ENABLE()        do{ __HAL_RCC_GPIOC_CLK_ENABLE(); }while(0)    /* 所在IO口时钟使能 */

#define SD_D3_GPIO_PORT                GPIOC
#define SD_D3_GPIO_PIN                 GPIO_PIN_11
#define SD_D3_GPIO_CLK_ENABLE()        do{ __HAL_RCC_GPIOC_CLK_ENABLE(); }while(0)    /* 所在IO口时钟使能 */

#define SD_CLK_GPIO_PORT               GPIOC
#define SD_CLK_GPIO_PIN                GPIO_PIN_12
#define SD_CLK_GPIO_CLK_ENABLE()       do{ __HAL_RCC_GPIOC_CLK_ENABLE(); }while(0)    /* 所在IO口时钟使能 */

#define SD_CMD_GPIO_PORT               GPIOD
#define SD_CMD_GPIO_PIN                GPIO_PIN_2
#define SD_CMD_GPIO_CLK_ENABLE()       do{ __HAL_RCC_GPIOD_CLK_ENABLE(); }while(0)    /* 所在IO口时钟使能 */

/******************************************************************************************/

#define SD_TIMEOUT             ((uint32_t)100000000)    /* 超时时间 */
#define SD_TRANSFER_OK         ((uint8_t)0x00)
#define SD_TRANSFER_BUSY       ((uint8_t)0x01)

/* 根据 SD_HandleTypeDef 定义的宏，用于快速计算容量 */
#define SD_TOTAL_SIZE_BYTE(__Handle__)  (((uint64_t)((__Handle__)->SdCard.LogBlockNbr) * ((__Handle__)->SdCard.LogBlockSize)) >> 0)
#define SD_TOTAL_SIZE_KB(__Handle__)    (((uint64_t)((__Handle__)->SdCard.LogBlockNbr) * ((__Handle__)->SdCard.LogBlockSize)) >> 10)
#define SD_TOTAL_SIZE_MB(__Handle__)    (((uint64_t)((__Handle__)->SdCard.LogBlockNbr) * ((__Handle__)->SdCard.LogBlockSize)) >> 20)
#define SD_TOTAL_SIZE_GB(__Handle__)    (((uint64_t)((__Handle__)->SdCard.LogBlockNbr) * ((__Handle__)->SdCard.LogBlockSize)) >> 30)

/*  
 *  SD传输时钟分频，由于HAL库运行效率低，很容易产生上溢（读SD卡时）/下溢错误（写SD卡时）
 *  使用4bit模式时，需降低SDIO时钟频率，将该宏改为 1，SDIO时钟频率：48/( SDIO_TRANSF_CLK_DIV + 2 ) = 16M * 4bit = 64Mbps 
 *  使用1bit模式时，该宏SDIO_TRANSF_CLK_DIV改为 0，SDIO时钟频率：48/( SDIO_TRANSF_CLK_DIV + 2 ) = 24M * 1bit = 24Mbps 
 */
#define  SDIO_TRANSF_CLK_DIV        1   
/******************************************************************************************/

extern SD_HandleTypeDef        g_sdcard_handler;        /* SD卡句柄 */
extern HAL_SD_CardInfoTypeDef  g_sd_card_info_handle;   /* SD卡信息结构体 */

/* 函数声明 */
uint8_t sd_init(void);                                              /* 初始化SD卡 */
uint8_t get_sd_card_info(HAL_SD_CardInfoTypeDef *cardinfo);         /* 获取卡信息函数 */
uint8_t get_sd_card_state(void);                                    /* 获取卡的状态 */
uint8_t sd_read_disk(uint8_t *pbuf, uint32_t saddr, uint32_t cnt);  /* 读SD卡 */
uint8_t sd_write_disk(uint8_t *pbuf, uint32_t saddr, uint32_t cnt); /* 写SD卡 */

#endif
