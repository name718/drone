/**
 * @file bsp.c
 * @brief 板级支持包实现 (160MHz 主频、SysTick 毫秒基准、LED 控制)
 */

#include "bsp.h"

#include "FreeRTOS.h"
#include "stm32g4xx.h"
#include "task.h"

// 声明 FreeRTOS 端口层的 SysTick 处理函数
extern void xPortSysTickHandler(void);

// 记录系统开机以来的毫秒数 (声明为 volatile，防止编译器过度优化)
static volatile uint32_t s_ticks_ms = 0;

void SysTick_Handler(void) {
    // 1. 裸机毫秒基准始终累加 (供 bsp_get_ticks_ms 使用)
    s_ticks_ms++;

    // 2. 如果操作系统已经启动运行，则同时推进 FreeRTOS 内核时钟节拍
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        xPortSysTickHandler();
    }
}
uint32_t bsp_get_ticks_ms(void) {
    return s_ticks_ms;
}

void delay_ms(uint32_t ms) {
    // 如果 FreeRTOS 调度器已经跑起来，调用 vTaskDelay 让出 CPU，不浪费算力
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    } else {
        // 调度器未启动前 (如开机初始化)，使用纯裸机死等防死锁
        uint32_t start = s_ticks_ms;
        while ((s_ticks_ms - start) < ms) {
            __NOP();
        }
    }
}
/**
 * @brief 将系统主频配置为满血 160MHz (HSI16 16MHz -> PLL -> 160MHz)
 */
static void clock_init(void) {
    // 0. 开启 PWR 调压器外设时钟，并使能 Range 1 Boost 模式 (突破 150MHz 限制，稳定运行 160MHz)
    RCC->APB1ENR1 |= RCC_APB1ENR1_PWREN;
    PWR->CR5 &= ~PWR_CR5_R1MODE;  // 清零 R1MODE 即进入 Boost 模式
    // 1. 启动内部 16MHz 高速时钟 (HSI16)
    RCC->CR |= RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY))
        ;

    // 2. 配置 Flash 读取等待周期 (160MHz 需 4 个等待周期) 并开启预取缓冲
    FLASH->ACR &= ~FLASH_ACR_LATENCY;
    FLASH->ACR |= FLASH_ACR_LATENCY_4WS | FLASH_ACR_PRFTEN;

    // 3. 配置 PLL: 16MHz / M(4) * N(80) / R(2) = 160MHz
    RCC->PLLCFGR = RCC_PLLCFGR_PLLSRC_HSI | (3U << RCC_PLLCFGR_PLLM_Pos)  // M = 4
                   | (80U << RCC_PLLCFGR_PLLN_Pos)                        // N = 80
                   | (0U << RCC_PLLCFGR_PLLR_Pos)                         // R = 2
                   | RCC_PLLCFGR_PLLREN;                                  // 使能 PLLR 主时钟输出

    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY))
        ;

    // 4. 切换系统时钟源到 PLL
    RCC->CFGR &= ~RCC_CFGR_SW;
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL)
        ;

    SystemCoreClock = 160000000U;

    // 5. 开启 SysTick 硬件中断，配置为每 1ms 触发一次
    SysTick_Config(SystemCoreClock / 1000U);
}

void led_set(bool on) {
    if (on) {
        GPIOC->BSRR = (1U << (13 + 16));  // PC13 输出低电平 (点亮)
    } else {
        GPIOC->BSRR = (1U << 13);  // PC13 输出高电平 (熄灭)
    }
}

void led_toggle(void) {
    if (GPIOC->ODR & (1U << 13)) {
        led_set(true);
    } else {
        led_set(false);
    }
}

void bsp_init(void) {
    // 1. 配置 160MHz 主频与 1ms 滴答中断
    clock_init();

    // 2. 开启 GPIOC 总线时钟并配置 PC13 为推挽输出
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOCEN;
    GPIOC->MODER &= ~(3U << (13 * 2));
    GPIOC->MODER |= (1U << (13 * 2));  // 输出模式
    led_set(false);                    // 默认熄灭
}
