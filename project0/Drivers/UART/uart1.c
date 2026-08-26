#include "uart1.h"
#include <stdbool.h>
#include "uart.h"
#include <stdio.h>

#include "driverlib/sysctl.h"
#include "tm4c123gh6pm.h"
#include <string.h>

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

  uint32_t available =
      (head >= tail) ? (head - tail) : (RX_BUFFER_SIZE - tail + head);
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
  GPIO_PORTB_DEN_R |= 0x03u;        // enable digital
  GPIO_PORTB_AMSEL_R &= ~0x03u;     // disable analog

  UART1_CTL_R &= ~0x01u; // disable UART1 while configuring

  UART1_IBRD_R = 8u;
  UART1_FBRD_R = 44u;

  UART1_LCRH_R = 0x70u;

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

void UART1_WriteRaw(const uint8_t *data, uint32_t len) {
  while (len--) {
    while ((UART1_FR_R & 0x20u) != 0u) {
    }
    UART1_DR_R = *data++;
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
  UART1_IpdStreamReset();

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

static void UART1_FlushRxFifo(void) {
  while ((UART1_FR_R & 0x10u) == 0u) {
    (void)UART1_DR_R;
  }
}

static int UART1_ReadByteTimeout(char *out, uint32_t timeout_ms) {
  uint32_t remaining = timeout_ms;

  while (remaining-- != 0u) {
    bool available = false;
    char byte = 0;

    UART1_IM_R &= ~0x50u; // stop RX ISR pushing into the buffer while reading

    if (rx_head != rx_tail) {
      byte = (char)rx_buffer[rx_tail];
      rx_tail = (rx_tail + 1u) % RX_BUFFER_SIZE;
      available = true;
    }

    UART1_IM_R |= 0x50u;

    if (available) {
      *out = byte;
      return 1;
    }
    UART1_DelayMs(1u);
  }

  return 0;
}

static void UART1_PrintDebug(char *label, const char *s) {
  UART0_WriteString(label);
  while (*s != '\0') {
    char c = *s++;
    switch (c) {
      case '\r':
        UART0_WriteString("<CR>");
        break;
      case '\n':
        UART0_WriteString("<LF>");
        break;
      default:
        UART0_WriteChar(c);
        break;
    }
  }
  UART0_WriteString("\r\n");
}

int AT_Send_Command(const char *cmd, const char *expected_resp,
                    uint32_t timeout_ms) {
  char full_cmd[128];
  char response[256];
  uint32_t cmd_len;
  uint32_t response_len = 0u;
  uint32_t elapsed_ms = 0u;
  int result = 0;

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

  UART1_PrintDebug("TX> ", full_cmd);

  UART1_ClearRxBuffer();
  UART1_FlushRxFifo();
  UART1_WriteString(full_cmd);

  while (elapsed_ms < timeout_ms) {
    char ch;

    if (UART1_ReadByteTimeout(&ch, 1u) != 0) {
      if (response_len < (sizeof(response) - 1u)) {
        response[response_len++] = ch;
        response[response_len] = '\0';
      }

      if ((strstr(response, expected_resp) != 0) ||
          ((strcmp(expected_resp, "OK") == 0) &&
           (strstr(response, "no change") != 0)) ||
          ((strcmp(expected_resp, "OK") == 0) &&
           ((strstr(response, "WIFI CONNECTED") != 0) ||
            (strstr(response, "WIFI GOT IP") != 0) ||
            (strstr(response, "CONNECTED") != 0)))) {
        result = 1;
        break;
      }

      if ((strstr(response, "ERROR") != 0) ||
          (strstr(response, "FAIL") != 0)) {
        result = 0;
        break;
      }
    }

    elapsed_ms++;
  }

  if (response_len > 0u) {
    UART1_PrintDebug("RX> ", response);
  }

  return result;
}

int UART1_CaptureResponse(char *dst, uint32_t dst_size, const char *term,
                          uint32_t timeout_ms) {
  uint32_t len = 0u;
  uint32_t elapsed_ms = 0u;

  if ((dst == 0) || (dst_size == 0u) || (term == 0)) {
    return 0;
  }

  dst[0] = '\0';

  while (elapsed_ms < timeout_ms) {
    char ch;

    if (UART1_ReadByteTimeout(&ch, 1u) != 0) {
      if (len < (dst_size - 1u)) {
        dst[len++] = ch;
        dst[len] = '\0';
      } else {
        // Buffer full: keep only the newest bytes so a trailing
        // terminator (e.g. "OK") is still found even for huge responses.
        memmove(dst, dst + 1u, dst_size - 2u);
        dst[dst_size - 2u] = ch;
        dst[dst_size - 1u] = '\0';
      }

      if (strstr(dst, term) != 0) {
        return 1;
      }
    }

    elapsed_ms++;
  }

  return 0;
}

uint32_t UART1_ReadIpdData(uint8_t *data, uint32_t data_size,
                           uint32_t timeout_ms) {
  char prefix[32];
  uint32_t prefix_len = 0u;
  uint32_t elapsed_ms = 0u;
  uint32_t remaining = 0u;
  bool in_data = false;
  uint32_t written = 0u;

  while (elapsed_ms < timeout_ms) {
    char ch;
    if (UART1_ReadByteTimeout(&ch, 1u) != 0) {
      if (!in_data) {
        if (prefix_len < (sizeof(prefix) - 1u)) {
          prefix[prefix_len++] = ch;
          prefix[prefix_len] = '\0';
        }

        char *ipd = strstr(prefix, "+IPD,");
        if (ipd != 0) {
          ipd += 5u;
          remaining = 0u;
          while ((*ipd != ':') && (*ipd != '\0')) {
            remaining = (remaining * 10u) + (uint32_t)(*ipd - '0');
            ipd++;
          }
          if (*ipd == ':') {
            in_data = true;
          }
        }
      } else {
        if (remaining > 0u) {
          if (written < data_size) {
            data[written++] = (uint8_t)ch;
          }
          remaining--;
          if (remaining == 0u) {
            return written;
          }
        }
      }
    }
    elapsed_ms++;
  }

  return written;
}

// Pop raw buffered bytes verbatim (no +IPD framing). Old NonOS AT firmware
// delivers continuation segments of one TCP receive as bare bytes, so MQTT
// reassembly needs a framing-free read path.
uint32_t UART1_ReadContinuation(uint8_t *data, uint32_t data_size,
                                uint32_t timeout_ms) {
  uint32_t written = 0u;
  uint32_t elapsed_ms = 0u;

  while ((written < data_size) && (elapsed_ms < timeout_ms)) {
    char ch;
    if (UART1_ReadByteTimeout(&ch, 1u) != 0) {
      data[written++] = (uint8_t)ch;
    } else if (written > 0u) {
      // Stream paused after delivering something - natural stop point.
      break;
    } else {
      elapsed_ms++;
    }
  }
  return written;
}

// ---------------------------------------------------------------------------
// TCP stream reader with persistent framing state.
//
// ESP8266 delivers socket data as "+IPD,<len>:<data>" frames, but headers and
// payload can be split across any number of reads at arbitrary byte
// boundaries. A parser with local state therefore loses sync whenever a read
// times out mid-header (observed as corrupted/truncated MQTT payloads), so
// this state machine keeps its position across calls.
// ---------------------------------------------------------------------------
#define IPD_TAG_LEN 5u
#define SEND_OK_TAG_LEN 7u

typedef struct {
  uint8_t mode;          // 0 = scanning for tag, 1 = length digits, 2 = data
  char hdr[IPD_TAG_LEN]; // sliding window of recent bytes while scanning
  uint32_t hdr_len;
  uint32_t remaining;
  char ok_win[SEND_OK_TAG_LEN]; // sliding window watching for "SEND OK"
  uint32_t ok_len;
  uint8_t send_ok;
} Uart1IpdState;

static Uart1IpdState g_ipd = {0u, {'\0'}, 0u, 0u, {'\0'}, 0u, 0u};

// Bytes captured while pumping the send-completion wait are parked here so
// the next framed read hands them out first instead of losing them.
static uint8_t g_prebuf[128];
static uint32_t g_prebuf_len = 0u;
static uint32_t g_prebuf_pos = 0u;

void UART1_IpdStreamReset(void) {
  g_ipd.mode = 0u;
  g_ipd.hdr_len = 0u;
  g_ipd.remaining = 0u;
  g_ipd.ok_len = 0u;
  g_ipd.send_ok = 0u;
  g_prebuf_len = 0u;
  g_prebuf_pos = 0u;
}

void UART1_PrependBytes(const uint8_t *data, uint32_t len) {
  if (len > sizeof(g_prebuf)) {
    uint32_t drop = len - sizeof(g_prebuf);
    data += drop;
    len = sizeof(g_prebuf);
  }
  memcpy(g_prebuf, data, len);
  g_prebuf_len = len;
  g_prebuf_pos = 0u;
}

uint8_t UART1_SendOkSeen(void) { return g_ipd.send_ok; }

static void Uart1_ScanWatch(const char ch) {
  if (g_ipd.ok_len >= SEND_OK_TAG_LEN) {
    memmove(g_ipd.ok_win, &g_ipd.ok_win[1], SEND_OK_TAG_LEN - 1u);
    g_ipd.ok_len = SEND_OK_TAG_LEN - 1u;
  }
  g_ipd.ok_win[g_ipd.ok_len++] = ch;
  if ((g_ipd.ok_len == SEND_OK_TAG_LEN) &&
      (memcmp(g_ipd.ok_win, "SEND OK", SEND_OK_TAG_LEN) == 0)) {
    g_ipd.send_ok = 1u;
  }
}

uint32_t UART1_ReadTcpBytes(uint8_t *data, uint32_t data_size,
                            uint32_t timeout_ms) {
  uint32_t written = 0u;
  uint32_t idle_ms = 0u;

  if ((data == 0) || (data_size == 0u)) {
    return 0u;
  }

  while (written < data_size) {
    char ch;

    // Pre-buffered bytes were already unwrapped from their +IPD frame when
    // captured - hand them straight out, never through the parser again.
    if (g_prebuf_pos < g_prebuf_len) {
      data[written++] = g_prebuf[g_prebuf_pos++];
      idle_ms = 0u;
      continue;
    }

    if (idle_ms >= timeout_ms) {
      break;
    }

    if (UART1_ReadByteTimeout(&ch, 1u) == 0) {
      idle_ms++;
      continue;
    }
    idle_ms = 0u;

    if (g_ipd.mode == 2u) {
      data[written++] = (uint8_t)ch;
      g_ipd.remaining--;
      if (g_ipd.remaining == 0u) {
        g_ipd.mode = 0u;
        g_ipd.hdr_len = 0u;
      }
    } else if (g_ipd.mode == 1u) {
      if ((ch >= '0') && (ch <= '9')) {
        g_ipd.remaining = (g_ipd.remaining * 10u) + (uint32_t)(ch - '0');
      } else if (ch == ':') {
        g_ipd.mode = 2u;
      } else {
        UART1_IpdStreamReset(); // malformed header - resync
      }
    } else {
      if (g_ipd.hdr_len < IPD_TAG_LEN) {
        g_ipd.hdr[g_ipd.hdr_len++] = ch;
      } else {
        memmove(g_ipd.hdr, &g_ipd.hdr[1], IPD_TAG_LEN - 1u);
        g_ipd.hdr[IPD_TAG_LEN - 1u] = ch;
      }
      if ((g_ipd.hdr_len == IPD_TAG_LEN) &&
          (memcmp(g_ipd.hdr, "+IPD,", IPD_TAG_LEN) == 0)) {
        g_ipd.mode = 1u;
        g_ipd.remaining = 0u;
      }
      Uart1_ScanWatch(ch);
    }
  }
  return written;
}

void UART1_GetRxBufferData(char *dst, uint32_t dst_size) {
  if ((dst == 0) || (dst_size == 0)) {
    return;
  }

  UART1_IM_R &= ~0x50u; // disable RX interrupts while reading buffer

  uint32_t head = rx_head;
  uint32_t tail = rx_tail;
  uint32_t available = (head >= tail) ? (head - tail) : (RX_BUFFER_SIZE - tail + head);

  uint32_t i = 0u;
  while ((i + 1u) < dst_size && available > 0u) {
    dst[i++] = rx_buffer[tail];
    tail = (tail + 1u) % RX_BUFFER_SIZE;
    available--;
  }

  dst[i] = '\0';

  // Clear the buffer
  rx_head = 0u;
  rx_tail = 0u;
  rx_buffer_overflow = false;

  UART1_IM_R |= 0x50u; // re-enable RX interrupts
}

bool UART1_WaitForPattern(const char *pattern, uint32_t timeout_ms) {
  if ((pattern == 0) || (timeout_ms == 0)) {
    return false;
  }

  char response[256];
  uint32_t response_len = 0u;
  uint32_t elapsed_ms = 0u;

  while (elapsed_ms < timeout_ms) {
    char ch;
    if (UART1_ReadByteTimeout(&ch, 1u) != 0) {
      if (response_len < (sizeof(response) - 1u)) {
        response[response_len++] = ch;
        response[response_len] = '\0';
      }

      if (strstr(response, pattern) != 0) {
        return true;
      }
    }

    elapsed_ms++;
  }

  return false;
}

bool UART1_WaitForPatternAndEcho(const char *pattern, uint32_t timeout_ms) {
  if ((pattern == 0) || (timeout_ms == 0)) {
    return false;
  }

  char response[256];
  uint32_t response_len = 0u;
  uint32_t elapsed_ms = 0u;

  while (elapsed_ms < timeout_ms) {
    char ch;
    if (UART1_ReadByteTimeout(&ch, 1u) != 0) {
      UART0_WriteChar(ch);
      if (response_len < (sizeof(response) - 1u)) {
        response[response_len++] = ch;
        response[response_len] = '\0';
      }

      if (strstr(response, pattern) != 0) {
        return true;
      }
    }

    elapsed_ms++;
  }

  return false;
}

void Display_Data_To_Putty(float humidity, float temperature) {
  char buf[80];
  // Format with one decimal place for readability
  int n = snprintf(buf, sizeof(buf), "Humidity: %.1f %%\r\nTemperature: %.1f C\r\n", humidity, temperature);
  if (n > 0) {
    UART0_WriteString(buf);
  }
}
