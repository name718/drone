#include "board.hpp"

#include <cstring>
uint32_t SystemCoreClock = 8000000u;
static volatile uint32_t ticks;
static bool hse_ok;
static bool lcd_software_spi;
static void (*service_hook)();
// 中断只搬字节，解析放在主循环。2的幂使索引回绕无需除法。
static uint8_t rx_ring[1024], tx_ring[256];
static volatile uint16_t rx_head, rx_tail, tx_head, tx_tail;
static volatile uint32_t rx_count, tx_count, uart_errors, tx_drops;
extern "C" void SystemInit() {
    // 芯片复位后保持 HSI；构造函数不碰硬件，正式时钟设置在 Board::init。
    SCB->VTOR = FLASH_BASE;
}
extern "C" void SystemCoreClockUpdate() {
    uint32_t config = RCC->CFGR, clock = 8000000u;
    if ((config & RCC_CFGR_SWS) == RCC_CFGR_SWS_PLL) {
        uint32_t multiplier = ((config & RCC_CFGR_PLLMULL) >> 18) + 2;
        if (multiplier > 16)
            multiplier = 16;
        clock = (config & RCC_CFGR_PLLSRC) ? 8000000u : 4000000u;
        if ((config & RCC_CFGR_PLLSRC) && (config & RCC_CFGR_PLLXTPRE))
            clock /= 2;
        clock *= multiplier;
    }
    static const uint8_t shifts[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 6, 7, 8, 9};
    SystemCoreClock = clock >> shifts[(config & RCC_CFGR_HPRE) >> 4];
}
extern "C" void _init() {}
extern "C" void _fini() {}
extern "C" void SysTick_Handler() {
    ++ticks;
}
extern "C" void USART1_IRQHandler() {
    uint32_t sr = USART1->SR;
    if (sr & (USART_SR_RXNE | USART_SR_ORE | USART_SR_NE | USART_SR_FE | USART_SR_PE)) {
        uint8_t byte = static_cast<uint8_t>(USART1->DR);  // 先读SR再读DR，清除错误标志。
        if (sr & (USART_SR_ORE | USART_SR_NE | USART_SR_FE | USART_SR_PE))
            ++uart_errors;
        else if (sr & USART_SR_RXNE) {
            uint16_t next = (rx_head + 1u) & 1023u;
            if (next == rx_tail)
                ++uart_errors;
            else {
                rx_ring[rx_head] = byte;
                __DMB();
                rx_head = next;
                ++rx_count;
            }
        }
    }
    if ((sr & USART_SR_TXE) && (USART1->CR1 & USART_CR1_TXEIE)) {
        if (tx_tail != tx_head) {
            USART1->DR = tx_ring[tx_tail];
            tx_tail = (tx_tail + 1u) & 255u;
            ++tx_count;
        } else
            USART1->CR1 &= ~USART_CR1_TXEIE;
    }
}
namespace {
void pin(GPIO_TypeDef *port, unsigned n, unsigned config) {
    volatile uint32_t &reg = n < 8 ? port->CRL : port->CRH;
    unsigned shift = (n & 7u) * 4;
    reg = (reg & ~(15u << shift)) | (config << shift);
}
void gpio(GPIO_TypeDef *port, unsigned n, bool high) {
    port->BSRR = 1u << (n + (high ? 0 : 16));
}
}  // namespace
namespace Board {
uint32_t millis() {
    return ticks;
}
bool hseClock() {
    return hse_ok;
}
void setServiceHook(void (*hook)()) {
    service_hook = hook;
}
void service() {
    if (service_hook)
        service_hook();
}
void delay(uint32_t ms) {
    uint32_t start = millis();
    while ((uint32_t)(millis() - start) < ms)
        service();
}
void init() {
    RCC->CR |= RCC_CR_HSEON;
    uint32_t limit = 1000000;
    while (!(RCC->CR & RCC_CR_HSERDY) && --limit) {
    }
    if (limit) {
        FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;
        RCC->CFGR = RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_PLLSRC | RCC_CFGR_PLLMULL9;
        RCC->CR |= RCC_CR_PLLON;
        limit = 1000000;
        while (!(RCC->CR & RCC_CR_PLLRDY) && --limit) {
        }
        if (limit) {
            RCC->CFGR |= RCC_CFGR_SW_PLL;
            limit = 1000000;
            while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL && --limit) {
            }
            hse_ok = limit != 0;
        }
    }
    if (!hse_ok) {
        // 晶振/PLL故障时回退内部8MHz，仍可回报状态；等待均有上限。
        RCC->CFGR &= ~RCC_CFGR_SW;
        limit = 1000000;
        while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI && --limit) {
        }
        if (!limit)
            NVIC_SystemReset();
        RCC->CR &= ~(RCC_CR_PLLON | RCC_CR_HSEON);
        RCC->CFGR = 0;
        FLASH->ACR = 0;
    }
    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000u);
    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN | RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN |
                    RCC_APB2ENR_IOPCEN | RCC_APB2ENR_SPI1EN | RCC_APB2ENR_USART1EN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
    RCC->AHBENR |= RCC_AHBENR_DMA1EN;
    // 先设置安全电平再切为输出，避免复位时的窄脉冲。
    gpio(GPIOA, 4, true);
    gpio(GPIOB, 0, true);
    gpio(GPIOB, 1, true);
    camera(false, true);
    pin(GPIOA, 4, 0x2);
    pin(GPIOA, 5, 0xb);
    pin(GPIOA, 7, 0xb);
    pin(GPIOB, 0, 0x2);
    pin(GPIOB, 1, 0x2);
    pin(GPIOB, 11, 0x2);
    pin(GPIOB, 12, 0x2);
    gpio(GPIOC, 13, true);
    pin(GPIOC, 13, 0x2);
    // 当前面包板/杜邦线实测：9MHz、4.5MHz白屏，1.125MHz可正常显示。
    // BR=101选择APB2时钟/64，复位后也使用已目视验证的速度；模式0、MSB先行。
    SPI1->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | SPI_CR1_BR_2 | SPI_CR1_BR_0 |
                SPI_CR1_BIDIMODE | SPI_CR1_BIDIOE | SPI_CR1_SPE;  // 72MHz/64=1.125MHz。
    pin(GPIOA, 9, 0xb);
    gpio(GPIOA, 10, true);
    pin(GPIOA, 10, 0x8);
    USART1->BRR = (SystemCoreClock + UART_BAUD / 2) / UART_BAUD;
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
    NVIC_SetPriority(USART1_IRQn, 1);
    NVIC_EnableIRQ(USART1_IRQn);
    gpio(GPIOA, 0, false);
    gpio(GPIOA, 1, false);
    pin(GPIOA, 0, 0xb);
    pin(GPIOA, 1, 0xb);
    // APB1虽是36MHz，但分频不为1时定时器时钟自动乘2，所以TIM2仍是72MHz。
    TIM2->PSC = SystemCoreClock / 1000000u - 1u;
    TIM2->ARR = 19999u;
    TIM2->CCMR1 = TIM_CCMR1_OC1PE | TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC2PE |
                  TIM_CCMR1_OC2M_1 | TIM_CCMR1_OC2M_2;
    TIM2->CCER = 0;
    TIM2->CCR1 = 0;
    TIM2->CCR2 = 0;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
    // 独立看门狗约2秒；调试暂停时冻结，便于学习单步调试。
    DBGMCU->CR |= DBGMCU_CR_DBG_IWDG_STOP;
    IWDG->KR = 0x5555;
    IWDG->PR = 4;
    IWDG->RLR = 1250;
    IWDG->KR = 0xaaaa;
    IWDG->KR = 0xcccc;
}
void watchdogKick() {
    IWDG->KR = 0xaaaa;
}
void lcdSelect(bool selected) {
    gpio(GPIOA, 4, !selected);
}
void lcdData(bool data) {
    gpio(GPIOB, 0, data);
}
void lcdReset(bool high) {
    gpio(GPIOB, 1, high);
}
void lcdDiagnosticMode(bool software_spi) {
    // 只在主循环的一帧结束后调用，此时不存在尚未完成的DMA事务。
    DMA1_Channel3->CCR = 0;
    SPI1->CR2 &= ~SPI_CR2_TXDMAEN;
    SPI1->CR1 &= ~SPI_CR1_SPE;
    if (software_spi) {
        gpio(GPIOA, 5, false);
        gpio(GPIOA, 7, false);
        pin(GPIOA, 5, 0x2);
        pin(GPIOA, 7, 0x2);
    } else {
        pin(GPIOA, 5, 0xb);
        pin(GPIOA, 7, 0xb);
        SPI1->CR1 |= SPI_CR1_SPE;
    }
    lcd_software_spi = software_spi;
}
void camera(bool reset_high, bool power_down) {
    gpio(GPIOB, 11, reset_high);
    gpio(GPIOB, 12, power_down);
}
void led(bool on) {
    gpio(GPIOC, 13, !on);
}
uint32_t rxBytes() {
    return rx_count;
}
uint32_t txBytes() {
    return tx_count;
}
uint32_t uartErrors() {
    return uart_errors;
}
uint32_t txDrops() {
    return tx_drops;
}
bool uartRead(uint8_t &byte) {
    if (rx_head == rx_tail)
        return false;
    byte = rx_ring[rx_tail];
    __DMB();
    rx_tail = (rx_tail + 1u) & 1023u;
    return true;
}
bool uartWrite(const uint8_t *data, size_t n) {
    uint32_t saved = __get_PRIMASK();
    __disable_irq();
    unsigned free = (tx_tail - tx_head - 1u) & 255u;
    if (n > free) {
        ++tx_drops;
        __set_PRIMASK(saved);
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        tx_ring[tx_head] = data[i];
        tx_head = (tx_head + 1u) & 255u;
    }
    USART1->CR1 |= USART_CR1_TXEIE;
    __set_PRIMASK(saved);
    return true;
}
void servo(bool enabled, uint16_t pan_us, uint16_t tilt_us) {
    if (!enabled) {
        TIM2->CCER = 0;
        return;
    }
    TIM2->CCR1 = pan_us;
    TIM2->CCR2 = tilt_us;
    if (!TIM2->CCER)
        TIM2->EGR = TIM_EGR_UG;
    TIM2->CCER = TIM_CCER_CC1E | TIM_CCER_CC2E;
}
bool spiWrite(const uint8_t *data, size_t n) {
    if (!n)
        return true;
    if (lcd_software_spi) {
        // 模式0、MSB先行。故意放慢边沿间隔，用来排除DMA及高速杜邦线问题。
        // 中断保持开启，UART继续收包；上层每行仍调用service喂狗并处理协议。
        for (size_t i = 0; i < n; ++i) {
            for (unsigned bit = 0; bit < 8; ++bit) {
                gpio(GPIOA, 7, (data[i] & (0x80u >> bit)) != 0);
                for (volatile unsigned wait = 0; wait < 16; ++wait)
                    __NOP();
                gpio(GPIOA, 5, true);
                for (volatile unsigned wait = 0; wait < 16; ++wait)
                    __NOP();
                gpio(GPIOA, 5, false);
            }
        }
        return true;
    }
    DMA1_Channel3->CCR = 0;
    DMA1->IFCR = DMA_IFCR_CGIF3;
    DMA1_Channel3->CPAR = reinterpret_cast<uintptr_t>(&SPI1->DR);
    DMA1_Channel3->CMAR = reinterpret_cast<uintptr_t>(data);
    DMA1_Channel3->CNDTR = n;
    SPI1->CR2 |= SPI_CR2_TXDMAEN;
    DMA1_Channel3->CCR = DMA_CCR_DIR | DMA_CCR_MINC | DMA_CCR_PL_1 | DMA_CCR_EN;
    uint32_t started = millis();
    while (!(DMA1->ISR & DMA_ISR_TCIF3)) {
        if ((uint32_t)(millis() - started) > 20u || (DMA1->ISR & DMA_ISR_TEIF3)) {
            DMA1_Channel3->CCR = 0;
            SPI1->CR2 &= ~SPI_CR2_TXDMAEN;
            return false;
        }
    }
    DMA1_Channel3->CCR = 0;
    SPI1->CR2 &= ~SPI_CR2_TXDMAEN;
    // DMA完成只说明最后一字节已经写入SPI_DR，不代表它已移上SCK/MOSI。
    // RM0008的发送收尾顺序是先TXE=1，再BSY=0；否则单字节命令可能
    // 尚未发完就切换DC/CS，屏幕会把命令误当数据。
    while (!(SPI1->SR & SPI_SR_TXE))
        if ((uint32_t)(millis() - started) > 20u)
            return false;
    while (SPI1->SR & SPI_SR_BSY)
        if ((uint32_t)(millis() - started) > 20u)
            return false;
    return true;
}
}  // namespace Board
