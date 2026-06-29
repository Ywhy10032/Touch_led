/**
  ******************************************************************************
  * @file    ws2812.c
  * @brief   WS2812 灯带驱动实现。
  *
  * 原理：WS2812 单总线归零码，每 1.25µs 发 1bit，靠高电平脉宽区分 0/1。
  * 每个 bit 映射成一个 PWM 周期的比较值(CCR)，用 DMA 把整段 CCR 数组逐周期
  * 搬入 TIM2->CCR1（PA0 / TIM2_CH1），由硬件精确产生波形。
  *
  * 时序（TIM2 时钟 72MHz，ARR=89 → 90 计数 = 1.25µs）：
  *   - 0 码高电平 = WS2812_CODE0 计数 ≈ 0.42µs
  *   - 1 码高电平 = WS2812_CODE1 计数 ≈ 0.83µs
  *   - 帧尾 WS2812_RESET 个 0 周期 ≈ 60µs，完成复位/锁存并使 PA0 收尾保持低电平
  ******************************************************************************
  */
#include "ws2812.h"
#include "tim.h"

#define WS2812_RESET   48                      /* 帧尾复位/锁存周期数（≈60µs） */
#define WS2812_BITS    (WS2812_NUM * 24)       /* 每颗 24bit (GRB) */
#define WS2812_BUFLEN  (WS2812_BITS + WS2812_RESET)
#define WS2812_CODE0   30                      /* 0 码高电平计数 ≈0.42µs */
#define WS2812_CODE1   60                      /* 1 码高电平计数 ≈0.83µs */

/* DMA 逐周期搬入 TIM2->CCR1 的比较值缓冲区；半字以匹配 DMA HALFWORD 对齐。
   静态变量零初始化，帧尾复位段始终保持 0，无需显式填充。 */
static uint16_t ws2812_buf[WS2812_BUFLEN];

void WS2812_SetPixel(uint32_t index, WS2812_Color_t color)
{
  if (index >= WS2812_NUM)
  {
    return;
  }

  /* WS2812 线序为 GRB，每字节 MSB 先发 */
  uint8_t grb[3] = { color.g, color.r, color.b };
  uint32_t idx = index * 24u;

  for (uint32_t c = 0; c < 3; c++)
  {
    for (int8_t bit = 7; bit >= 0; bit--)
    {
      ws2812_buf[idx++] = (grb[c] & (1u << bit)) ? WS2812_CODE1 : WS2812_CODE0;
    }
  }
}

void WS2812_SetAll(WS2812_Color_t color)
{
  for (uint32_t i = 0; i < WS2812_NUM; i++)
  {
    WS2812_SetPixel(i, color);
  }
}

void WS2812_SetAllRGB(uint8_t r, uint8_t g, uint8_t b)
{
  WS2812_Color_t color = { r, g, b };
  WS2812_SetAll(color);
}

void WS2812_Clear(void)
{
  WS2812_SetAll(WS2812_BLACK);
}

void WS2812_Send(void)
{
  /* Normal 模式下传输完成时 HAL 会在 DMA1_Channel5_IRQHandler 中自动关闭 CC1
     的 DMA 请求并把通道状态置回 READY，因此每次刷新可直接再次调用本函数。 */
  HAL_TIM_PWM_Start_DMA(&htim2, TIM_CHANNEL_1, (uint32_t *)ws2812_buf, WS2812_BUFLEN);
}
