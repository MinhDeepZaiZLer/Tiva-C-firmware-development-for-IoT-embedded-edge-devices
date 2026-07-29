#include "uart1.h"
#include <stdbool.h>
#include <string.h>
#include "tm4c123gh6pm.h"
#include "driverlib/sysctl.h"

static volatile char rx_buffer[RX_BUFFER_SIZE];
static volatile uint32_t rx_head = 0u;
static volatile uint32_t rx_tail = 0u;
static volatile bool rx_buffer_overflow = false;

static inline uint32_t UART1_TicksPerMs(void) {
    uint32_t system_clock = SysCtlClockGet();
    return (system_clock + 2999u) / 3000u;
}

static inline void UART1_DelayMs(uint32_t ms) {
    uint32_t ticks = UART1_TicksPerMs();
    while (ms--) {
        SysCtlDelay(ticks);
    }
}

static bool UART1_RxBufferContainsPattern(const char *pattern) {
    uint32_t pattern_len = strlen(pattern);
    if (pattern_len == 0u) {
        return false;
    }

    uint32_t head = rx_head;
    uint32_t tail = rx_tail;
    if (tail == head) {
        return false;
    }

    uint32_t available = (head >= tail)
                             ? (head - tail)
                             : (RX_BUFFER_SIZE - tail + head);
    if (pattern_len > available) {
        return false;
    }

    uint32_t start = tail;
    while (start != head) {
        uint32_t scan = start;
        uint32_t match = 0u;

        while ((match < pattern_len) && (scan != head) &&
               (rx_buffer[scan] == pattern[match])) {
            scan = (scan + 1u) % RX_BUFFER_SIZE;
            match++;
        }

        if (match == pattern_len) {
            return true;
        }

        start = (start + 1u) % RX_BUFFER_SIZE;
    }

    return false;
}

void UART1_Init(void) {
    SYSCTL_RCGCUART_R |= 0x02u; // UART1 clock
    SYSCTL_RCGCGPIO_R |= 0x02u; // Port B clock

    while ((SYSCTL_PRUART_R & 0x02u) == 0u) {
    }
    while ((SYSCTL_PRGPIO_R & 0x02u) == 0u) {
    }

    GPIO_PORTB_AFSEL_R |= 0x03u; // enable alternate function on PB0, PB1
    GPIO_PORTB_PCTL_R &= ~0x000000FFu;
    GPIO_PORTB_PCTL_R |= 0x00000011u; // select UART function
    GPIO_PORTB_DEN_R |= 0x03u;       // enable digital
    GPIO_PORTB_AMSEL_R &= ~0x03u;    // disable analog

    UART1_CTL_R &= ~0x01u; // disable UART1 while configuring

    UART1_IBRD_R = 8u;
    UART1_FBRD_R = 44u;

    UART1_LCRH_R = 0x60u;

    UART1_IM_R &= ~0x50u;
    UART1_ICR_R = 0x50u;

    UART1_IM_R |= 0x50u;
    NVIC_EN0_R |= (1u << 6u);

    UART1_CTL_R |= 0x0301u; // enable TX/RX and UART
}

void UART1_WriteChar(char c) {
    while ((UART1_FR_R & 0x20u) != 0u) {
    }
    UART1_DR_R = c;
}

void UART1_WriteString(const char *str) {
    while (*str != '\0') {
        UART1_WriteChar(*str++);
    }
}

char UART1_ReadChar(void) {
    while ((UART1_FR_R & 0x10u) != 0u) {
    }
    return (char)(UART1_DR_R & 0xFFu);
}

void UART1_ClearRxBuffer(void) {
    UART1_IM_R &= ~0x50u;
    UART1_ICR_R = 0x50u;

    rx_head = 0u;
    rx_tail = 0u;
    rx_buffer_overflow = false;

    UART1_IM_R |= 0x50u;
}

void UART1_Handler(void) {
    UART1_ICR_R = 0x50u;

    while ((UART1_FR_R & 0x10u) == 0u) {
        char c = (char)(UART1_DR_R & 0xFFu);
        uint32_t next_head = (rx_head + 1u) % RX_BUFFER_SIZE;

        if (next_head == rx_tail) {
            rx_tail = (rx_tail + 1u) % RX_BUFFER_SIZE;
            rx_buffer_overflow = true;
        }

        rx_buffer[rx_head] = c;
        rx_head = next_head;
    }
}

int AT_Send_Command(const char *cmd, const char *expected_resp, uint32_t timeout_ms) {
    char full_cmd[128];
    uint32_t cmd_len;

    if ((cmd == 0) || (expected_resp == 0)) {
        return 0;
    }

    cmd_len = strlen(cmd);
    if ((cmd_len + 2u) >= sizeof(full_cmd)) {
        return 0;
    }

    memcpy(full_cmd, cmd, cmd_len);
    full_cmd[cmd_len] = '\r';
    full_cmd[cmd_len + 1u] = '\n';
    full_cmd[cmd_len + 2u] = '\0';

    UART1_ClearRxBuffer();
    UART1_WriteString(full_cmd);

    while (timeout_ms-- != 0u) {
        if (UART1_RxBufferContainsPattern(expected_resp)) {
            return 1;
        }

        if (UART1_RxBufferContainsPattern("ERROR")) {
            return 0;
        }

        UART1_DelayMs(1u);
    }

    return 0;
}
