#ifndef BUTTON_H
#define BUTTON_H

#include <stdint.h>

// 按鈕 GPIO 定義
#define BUTTON_UP 38
#define BUTTON_DOWN 39
#define BUTTON_CENTER 0

// 組合鍵虛擬 ID
#define BUTTON_COMBO_UP_DOWN 100

// 按鈕事件類型
#define BUTTON_SHORT_PRESS 0
#define BUTTON_LONG_PRESS 1

// 按鈕事件回調函數類型
typedef void (*button_callback_t)(uint8_t button_id, uint8_t event_type);

/**
 * @brief 初始化按鈕模組
 */
void button_init(void);

/**
 * @brief 註冊按鈕事件回調函數
 * @param callback 回調函數指針
 */
void button_register_callback(button_callback_t callback);

/**
 * @brief 反初始化按鈕模組
 */
void button_deinit(void);

#endif