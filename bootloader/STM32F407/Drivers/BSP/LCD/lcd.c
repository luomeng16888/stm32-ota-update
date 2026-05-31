/**
 * lcd.c — ???(?ST7789,Bootloader??)
 */

#include "stdlib.h"
#include "./BSP/LCD/lcd.h"
#include "./BSP/LCD/lcdfont.h"
#include "./SYSTEM/usart/usart.h"
#include "./SYSTEM/delay/delay.h"  
SRAM_HandleTypeDef g_sram_handle;

uint32_t g_point_color = 0xF800;
uint32_t g_back_color  = 0xFFFF;

_lcd_dev lcddev;

/* ===== ???? ===== */

void lcd_wr_data(volatile uint16_t data)
{
    data = data;
    LCD->LCD_RAM = data;
}

void lcd_wr_regno(volatile uint16_t regno)
{
    regno = regno;
    LCD->LCD_REG = regno;
}

void lcd_write_reg(uint16_t regno, uint16_t data)
{
    LCD->LCD_REG = regno;
    LCD->LCD_RAM = data;
}

static void lcd_opt_delay(uint32_t i)
{
    while (i--);
}


static uint16_t lcd_rd_data(void)
{
    volatile uint16_t ram;
    lcd_opt_delay(2);
    ram = LCD->LCD_RAM;
    return ram;
}

void lcd_write_ram_prepare(void)
{
    LCD->LCD_REG = lcddev.wramcmd;
}

/* ===== ST7789?????? ===== */

static void lcd_ex_st7789_reginit(void)
{
    lcd_wr_regno(0x11);
    delay_ms(120);

    lcd_wr_regno(0x36);
    lcd_wr_data(0x00);

    lcd_wr_regno(0x3A);
    lcd_wr_data(0x05);

    lcd_wr_regno(0xB2);
    lcd_wr_data(0x0C);
    lcd_wr_data(0x0C);
    lcd_wr_data(0x00);
    lcd_wr_data(0x33);
    lcd_wr_data(0x33);

    lcd_wr_regno(0xB7);
    lcd_wr_data(0x35);

    lcd_wr_regno(0xBB);
    lcd_wr_data(0x19);

    lcd_wr_regno(0xC0);
    lcd_wr_data(0x2C);

    lcd_wr_regno(0xC2);
    lcd_wr_data(0x01);

    lcd_wr_regno(0xC3);
    lcd_wr_data(0x12);

    lcd_wr_regno(0xC4);
    lcd_wr_data(0x20);

    lcd_wr_regno(0xC6);
    lcd_wr_data(0x0F);

    lcd_wr_regno(0xD0);
    lcd_wr_data(0xA4);
    lcd_wr_data(0xA1);

    lcd_wr_regno(0x21);

    lcd_wr_regno(0xE0);
    lcd_wr_data(0xD0);
    lcd_wr_data(0x04);
    lcd_wr_data(0x0D);
    lcd_wr_data(0x11);
    lcd_wr_data(0x13);
    lcd_wr_data(0x2B);
    lcd_wr_data(0x3F);
    lcd_wr_data(0x54);
    lcd_wr_data(0x4C);
    lcd_wr_data(0x18);
    lcd_wr_data(0x0D);
    lcd_wr_data(0x0B);
    lcd_wr_data(0x1F);
    lcd_wr_data(0x23);

    lcd_wr_regno(0xE1);
    lcd_wr_data(0xD0);
    lcd_wr_data(0x04);
    lcd_wr_data(0x0C);
    lcd_wr_data(0x11);
    lcd_wr_data(0x13);
    lcd_wr_data(0x2C);
    lcd_wr_data(0x3F);
    lcd_wr_data(0x44);
    lcd_wr_data(0x51);
    lcd_wr_data(0x2F);
    lcd_wr_data(0x1F);
    lcd_wr_data(0x1F);
    lcd_wr_data(0x20);
    lcd_wr_data(0x23);

    lcd_wr_regno(0x29);
}

/* ===== ???? ===== */

void lcd_set_cursor(uint16_t x, uint16_t y)
{
    lcd_wr_regno(lcddev.setxcmd);
    lcd_wr_data(x >> 8);
    lcd_wr_data(x & 0xFF);
    lcd_wr_regno(lcddev.setycmd);
    lcd_wr_data(y >> 8);
    lcd_wr_data(y & 0xFF);
}

void lcd_scan_dir(uint8_t dir)
{
    uint16_t regval = 0;
    uint16_t temp;

    if (lcddev.dir == 1)
    {
        switch (dir)
        {
            case 0: dir = 6; break;
            case 1: dir = 7; break;
            case 2: dir = 4; break;
            case 3: dir = 5; break;
            case 4: dir = 1; break;
            case 5: dir = 0; break;
            case 6: dir = 3; break;
            case 7: dir = 2; break;
        }
    }

    switch (dir)
    {
        case L2R_U2D: regval |= (0 << 7) | (0 << 6) | (0 << 5); break;
        case L2R_D2U: regval |= (1 << 7) | (0 << 6) | (0 << 5); break;
        case R2L_U2D: regval |= (0 << 7) | (1 << 6) | (0 << 5); break;
        case R2L_D2U: regval |= (1 << 7) | (1 << 6) | (0 << 5); break;
        case U2D_L2R: regval |= (0 << 7) | (0 << 6) | (1 << 5); break;
        case U2D_R2L: regval |= (0 << 7) | (1 << 6) | (1 << 5); break;
        case D2U_L2R: regval |= (1 << 7) | (0 << 6) | (1 << 5); break;
        case D2U_R2L: regval |= (1 << 7) | (1 << 6) | (1 << 5); break;
    }

    regval |= 0x08; /* ST7789 BGR? */
    lcd_write_reg(0x36, regval);

    if (regval & 0x20)
    {
        if (lcddev.width < lcddev.height)
        {
            temp = lcddev.width;
            lcddev.width = lcddev.height;
            lcddev.height = temp;
        }
    }
    else
    {
        if (lcddev.width > lcddev.height)
        {
            temp = lcddev.width;
            lcddev.width = lcddev.height;
            lcddev.height = temp;
        }
    }

    lcd_wr_regno(lcddev.setxcmd);
    lcd_wr_data(0);
    lcd_wr_data(0);
    lcd_wr_data((lcddev.width - 1) >> 8);
    lcd_wr_data((lcddev.width - 1) & 0xFF);
    lcd_wr_regno(lcddev.setycmd);
    lcd_wr_data(0);
    lcd_wr_data(0);
    lcd_wr_data((lcddev.height - 1) >> 8);
    lcd_wr_data((lcddev.height - 1) & 0xFF);
}

void lcd_display_dir(uint8_t dir)
{
    lcddev.dir = dir;

    if (dir == 0)
    {
        lcddev.width = 240;
        lcddev.height = 320;
    }
    else
    {
        lcddev.width = 320;
        lcddev.height = 240;
    }

    lcddev.wramcmd = 0x2C;
    lcddev.setxcmd = 0x2A;
    lcddev.setycmd = 0x2B;

    lcd_scan_dir(DFT_SCAN_DIR);
}

void lcd_draw_point(uint16_t x, uint16_t y, uint32_t color)
{
    lcd_set_cursor(x, y);
    lcd_write_ram_prepare();
    LCD->LCD_RAM = color;
}

/* ===== SRAM????? ===== */

void HAL_SRAM_MspInit(SRAM_HandleTypeDef *hsram)
{
    GPIO_InitTypeDef gpio_init_struct;

    __HAL_RCC_FSMC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    gpio_init_struct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_8 \
                           | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_14 | GPIO_PIN_15;
    gpio_init_struct.Mode = GPIO_MODE_AF_PP;
    gpio_init_struct.Pull = GPIO_PULLUP;
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio_init_struct.Alternate = GPIO_AF12_FSMC;
    HAL_GPIO_Init(GPIOD, &gpio_init_struct);

    gpio_init_struct.Pin = GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 \
                           | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOE, &gpio_init_struct);
}

/* ===== LCD???(?ST7789) ===== */

void lcd_init(void)
{
    GPIO_InitTypeDef gpio_init_struct;
    FSMC_NORSRAM_TimingTypeDef fsmc_read_handle;
    FSMC_NORSRAM_TimingTypeDef fsmc_write_handle;

    LCD_CS_GPIO_CLK_ENABLE();
    LCD_WR_GPIO_CLK_ENABLE();
    LCD_RD_GPIO_CLK_ENABLE();
    LCD_RS_GPIO_CLK_ENABLE();
    LCD_BL_GPIO_CLK_ENABLE();

    gpio_init_struct.Pin = LCD_CS_GPIO_PIN;
    gpio_init_struct.Mode = GPIO_MODE_AF_PP;
    gpio_init_struct.Pull = GPIO_PULLUP;
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio_init_struct.Alternate = GPIO_AF12_FSMC;
    HAL_GPIO_Init(LCD_CS_GPIO_PORT, &gpio_init_struct);

    gpio_init_struct.Pin = LCD_WR_GPIO_PIN;
    HAL_GPIO_Init(LCD_WR_GPIO_PORT, &gpio_init_struct);

    gpio_init_struct.Pin = LCD_RD_GPIO_PIN;
    HAL_GPIO_Init(LCD_RD_GPIO_PORT, &gpio_init_struct);

    gpio_init_struct.Pin = LCD_RS_GPIO_PIN;
    HAL_GPIO_Init(LCD_RS_GPIO_PORT, &gpio_init_struct);

    gpio_init_struct.Pin = LCD_BL_GPIO_PIN;
    gpio_init_struct.Mode = GPIO_MODE_OUTPUT_PP;
    HAL_GPIO_Init(LCD_BL_GPIO_PORT, &gpio_init_struct);

    g_sram_handle.Instance = FSMC_NORSRAM_DEVICE;
    g_sram_handle.Extended = FSMC_NORSRAM_EXTENDED_DEVICE;
    g_sram_handle.Init.NSBank = FSMC_NORSRAM_BANK4;
    g_sram_handle.Init.DataAddressMux = FSMC_DATA_ADDRESS_MUX_DISABLE;
    g_sram_handle.Init.MemoryDataWidth = FSMC_NORSRAM_MEM_BUS_WIDTH_16;
    g_sram_handle.Init.BurstAccessMode = FSMC_BURST_ACCESS_MODE_DISABLE;
    g_sram_handle.Init.WaitSignalPolarity = FSMC_WAIT_SIGNAL_POLARITY_LOW;
    g_sram_handle.Init.WaitSignalActive = FSMC_WAIT_TIMING_BEFORE_WS;
    g_sram_handle.Init.WriteOperation = FSMC_WRITE_OPERATION_ENABLE;
    g_sram_handle.Init.WaitSignal = FSMC_WAIT_SIGNAL_DISABLE;
    g_sram_handle.Init.ExtendedMode = FSMC_EXTENDED_MODE_ENABLE;
    g_sram_handle.Init.AsynchronousWait = FSMC_ASYNCHRONOUS_WAIT_DISABLE;
    g_sram_handle.Init.WriteBurst = FSMC_WRITE_BURST_DISABLE;

    fsmc_read_handle.AddressSetupTime = 0x0F;
    fsmc_read_handle.AddressHoldTime = 0x00;
    fsmc_read_handle.DataSetupTime = 60;
    fsmc_read_handle.AccessMode = FSMC_ACCESS_MODE_A;

    fsmc_write_handle.AddressSetupTime = 9;
    fsmc_write_handle.AddressHoldTime = 0x00;
    fsmc_write_handle.DataSetupTime = 9;
    fsmc_write_handle.AccessMode = FSMC_ACCESS_MODE_A;

    HAL_SRAM_Init(&g_sram_handle, &fsmc_read_handle, &fsmc_write_handle);
    delay_ms(50);

    /* ??LCD ID */
    lcd_wr_regno(0x04);
    lcddev.id = lcd_rd_data();
    lcddev.id = lcd_rd_data();
    lcddev.id = lcd_rd_data();
    lcddev.id <<= 8;
    lcddev.id |= lcd_rd_data();

    if (lcddev.id == 0x8552)
    {
        lcddev.id = 0x7789;
    }

    printf("LCD ID:%x\r\n", lcddev.id);

    if (lcddev.id == 0x7789)
    {
        lcd_ex_st7789_reginit();
    }
    else
    {
        printf("[BOOT] Unsupported LCD!\r\n");
    }

    /* ????? */
    fsmc_write_handle.AddressSetupTime = 3;
    fsmc_write_handle.DataSetupTime = 3;
    FSMC_NORSRAM_Extended_Timing_Init(g_sram_handle.Extended, &fsmc_write_handle, g_sram_handle.Init.NSBank, g_sram_handle.Init.ExtendedMode);

    lcd_display_dir(0);
    LCD_BL(1);
    lcd_clear(WHITE);
}

/* ===== ???? ===== */

void lcd_clear(uint16_t color)
{
    uint32_t index = 0;
    uint32_t totalpoint = lcddev.width;
    totalpoint *= lcddev.height;

    lcd_set_cursor(0x00, 0x0000);
    lcd_write_ram_prepare();

    for (index = 0; index < totalpoint; index++)
    {
        LCD->LCD_RAM = color;
    }
}

void lcd_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint32_t color)
{
    uint16_t i, j;
    uint16_t xlen = ex - sx + 1;

    for (i = sy; i <= ey; i++)
    {
        lcd_set_cursor(sx, i);
        lcd_write_ram_prepare();
        for (j = 0; j < xlen; j++)
        {
            LCD->LCD_RAM = color;
        }
    }
}

/* ===== ???? ===== */

void lcd_show_char(uint16_t x, uint16_t y, char chr, uint8_t size, uint8_t mode, uint16_t color)
{
    uint8_t temp, t1, t;
    uint16_t y0 = y;
    uint8_t csize = 0;
    uint8_t *pfont = 0;

    csize = (size / 8 + ((size % 8) ? 1 : 0)) * (size / 2);
    chr = chr - ' ';

    switch (size)
    {
        case 12: pfont = (uint8_t *)asc2_1206[chr]; break;
        case 16: pfont = (uint8_t *)asc2_1608[chr]; break;
        case 24: pfont = (uint8_t *)asc2_2412[chr]; break;
        case 32: pfont = (uint8_t *)asc2_3216[chr]; break;
        default: return;
    }

    for (t = 0; t < csize; t++)
    {
        temp = pfont[t];
        for (t1 = 0; t1 < 8; t1++)
        {
            if (temp & 0x80)
            {
                lcd_draw_point(x, y, color);
            }
            else if (mode == 0)
            {
                lcd_draw_point(x, y, g_back_color);
            }
            temp <<= 1;
            y++;
            if (y >= lcddev.height) return;
            if ((y - y0) == size)
            {
                y = y0;
                x++;
                if (x >= lcddev.width) return;
                break;
            }
        }
    }
}

void lcd_show_string(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t size, char *p, uint16_t color)
{
    uint8_t x0 = x;
    width += x;
    height += y;

    while ((*p <= '~') && (*p >= ' '))
    {
        if (x >= width)
        {
            x = x0;
            y += size;
        }
        if (y >= height) break;
        lcd_show_char(x, y, *p, size, 0, color);
        x += size / 2;
        p++;
    }
}
