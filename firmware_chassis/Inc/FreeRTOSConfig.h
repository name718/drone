#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>

// --- 1. 核心时钟与调度节拍 ---
#define configUSE_PREEMPTION 1                     // 使能抢占式调度器 (硬实时必备)
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1  // 启用 Cortex-M 硬件 CLZ 汇编极速选任务
#define configCPU_CLOCK_HZ ((uint32_t)160000000)   // 核心主频 160MHz
#define configTICK_RATE_HZ ((TickType_t)1000)      // 调度节拍 1000Hz (1ms 滴答)
#define configMAX_PRIORITIES (7)                   // 优先级数量 0~6 (数字越大优先级越高)
#define configMINIMAL_STACK_SIZE ((uint16_t)128)   // 空闲任务栈大小 (128字 = 512字节)
#define configMAX_TASK_NAME_LEN (16)               // 任务名称最大长度
#define configUSE_16_BIT_TICKS 0                   // 32 位系统节拍计数器
#define configIDLE_SHOULD_YIELD 1                  // 空闲任务主动让出 CPU

// --- 2. 内存管理 ---
#define configSUPPORT_DYNAMIC_ALLOCATION 1  // 启用动态内存分配 (xTaskCreate)
#define configSUPPORT_STATIC_ALLOCATION 0
#define configTOTAL_HEAP_SIZE \
    ((size_t)(32 * 1024))  // 分配 32KB 堆内存给操作系统 (STM32G473 共有 128KB SRAM)

// --- 3. 辅助功能与钩子函数 ---
#define configUSE_MUTEXES 1  // 使能互斥量
#define configUSE_RECURSIVE_MUTEXES 1
#define configUSE_COUNTING_SEMAPHORES 1
#define configUSE_QUEUE_SETS 1
#define configUSE_IDLE_HOOK 0
#define configUSE_TICK_HOOK 0
#define configCHECK_FOR_STACK_OVERFLOW 0  // 调试初期暂不开启栈溢出钩子
#define configUSE_MALLOC_FAILED_HOOK 0

// --- 4. 软件定时器 ---
#define configUSE_TIMERS 1
#define configTIMER_TASK_PRIORITY (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH 5
#define configTIMER_TASK_STACK_DEPTH (configMINIMAL_STACK_SIZE * 2)

// --- 5. Cortex-M4 中断优先级机制 (最关键的安全防线) ---
// STM32 的 NVIC 采用 4 位优先级 (0~15)，数值越小优先级越高
#ifdef __NVIC_PRIO_BITS
#define configPRIO_BITS __NVIC_PRIO_BITS
#else
#define configPRIO_BITS 4
#endif

#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5

// 硬件寄存器层面的移位换算 (NVIC 靠高 4 位对齐)
#define configKERNEL_INTERRUPT_PRIORITY \
    (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

// --- 6. 中断服务函数重映射 ---
// 将 FreeRTOS 内核的底层中断处理函数映射到 STM32 启动向量表中
#define vPortSVCHandler SVC_Handler
#define xPortPendSVHandler PendSV_Handler
// 注意：SysTick_Handler 我们在 bsp.c 中接管，以实现双轨时基，不在此处宏重命名

// --- 7. 可选 API 函数使能 (必须定义为 1 才会把对应函数编译进固件) ---
#define INCLUDE_vTaskPrioritySet 1
#define INCLUDE_uxTaskPriorityGet 1
#define INCLUDE_vTaskDelete 1
#define INCLUDE_vTaskCleanUpResources 0
#define INCLUDE_vTaskSuspend 1
// #define INCLUDE_vTaskDelayUntil 1
#define INCLUDE_xTaskDelayUntil 1
#define INCLUDE_vTaskDelay 1
#define INCLUDE_xTaskGetSchedulerState 1

#endif /* FREERTOS_CONFIG_H */
