#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

// ===== 显示屏参数 (ST7701S RGB) =====
#define DISPLAY_WIDTH               480
#define DISPLAY_HEIGHT              480
#define DISPLAY_MIRROR_X            0   // 参考 ESPHome 配置
#define DISPLAY_MIRROR_Y            0
#define DISPLAY_SWAP_XY             0  // 方形屏通常无需交换


// RGB 数据总线引脚 (16-bit RGB565)
//依次定义 R1-R4, G0-G5, B0-B4 (共16根数据线，根据参考项目引脚表)
#define LCD_RGB_DATA_PIN_NUM_R0     GPIO_NUM_4   //DATA0
#define LCD_RGB_DATA_PIN_NUM_R1     GPIO_NUM_5   //DATA1
#define LCD_RGB_DATA_PIN_NUM_R2     GPIO_NUM_6   //DATA2
#define LCD_RGB_DATA_PIN_NUM_R3     GPIO_NUM_7   //DATA3
#define LCD_RGB_DATA_PIN_NUM_R4     GPIO_NUM_15  //DATA4
#define LCD_RGB_DATA_PIN_NUM_G0     GPIO_NUM_8   //DATA5
#define LCD_RGB_DATA_PIN_NUM_G1     GPIO_NUM_20  //DATA6
#define LCD_RGB_DATA_PIN_NUM_G2     GPIO_NUM_3   //DATA7
#define LCD_RGB_DATA_PIN_NUM_G3     GPIO_NUM_46  //DATA8
#define LCD_RGB_DATA_PIN_NUM_G4     GPIO_NUM_9   //DATA9
#define LCD_RGB_DATA_PIN_NUM_G5     GPIO_NUM_10  //DATA10
#define LCD_RGB_DATA_PIN_NUM_B0     GPIO_NUM_11  //DATA11
#define LCD_RGB_DATA_PIN_NUM_B1     GPIO_NUM_12  //DATA12
#define LCD_RGB_DATA_PIN_NUM_B2     GPIO_NUM_13  //DATA13
#define LCD_RGB_DATA_PIN_NUM_B3     GPIO_NUM_14  //DATA14
#define LCD_RGB_DATA_PIN_NUM_B4     GPIO_NUM_0   //DATA15

// 控制信号
#define LCD_RGB_DE_PIN_NUM          GPIO_NUM_18
#define LCD_RGB_VSYNC_PIN_NUM       GPIO_NUM_17
#define LCD_RGB_HSYNC_PIN_NUM       GPIO_NUM_16
#define LCD_RGB_PCLK_PIN_NUM        GPIO_NUM_21


// 3-Wire SPI (仅用于 ST7701S 初始化)
#define LCD_SPI_CS_PIN_NUM          GPIO_NUM_39
#define LCD_SPI_SCK_PIN_NUM         GPIO_NUM_48
#define LCD_SPI_SDA_PIN_NUM         GPIO_NUM_47

// 背光
#define DISPLAY_BACKLIGHT_PIN       GPIO_NUM_38
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false


// ===== 触摸 (GT911 I2C) =====
#define TP_I2C_SDA_PIN              GPIO_NUM_19
#define TP_I2C_SCL_PIN              GPIO_NUM_45


// Microphone: INMP441 on header P3, L/R tied to GND.
#define AUDIO_I2S_MIC_GPIO_SCK GPIO_NUM_6
#define AUDIO_I2S_MIC_GPIO_WS GPIO_NUM_7
#define AUDIO_I2S_MIC_GPIO_DIN GPIO_NUM_15


// MSM261
#define MSM261_BCLK 7
#define MSM261_WS 9
#define MSM261_DATA 8

#endif
