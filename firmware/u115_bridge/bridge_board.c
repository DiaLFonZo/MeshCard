/*
  Low-level board code for the U115 bridge (ASR6501 = PSoC4 + SX1262).

  Heltec's CubeCell Arduino layer runs a Heltec license check inside
  pinMode(), and almost everything uses pinMode() (Serial.begin, GpioInit,
  digitalWrite, attachInterrupt, boardInitMcu, the timer interrupt). On a chip
  without a Heltec key (the U115's Ai-Thinker module has none) it freezes.
  So the bridge does not use that layer at all: it drives the chip directly
  through the PSoC registers and the Cypress-generated SCB drivers
  (UART_1 = Grove serial, SPI_1 = internal radio), none of which check.
*/
#include <project.h>

#define REG32(a) (*(volatile uint32_t *)(a))

/* ---- pins ----
   P3.0 UART RX, P3.1 UART TX (Grove), P3.2/P3.3 SWD
   P4.0 MOSI, P4.1 MISO, P4.2 SCK (SPI_1, set up by SPI_1_Start)
   P4.3 radio NSS, P4.6 radio DIO1, P4.7 radio BUSY, P5.7 radio RESET,
   P6.1 antenna switch power (must be high) */
#define HSIOM_SEL(p)  (0x40020000u + (p) * 0x100u)
#define GPIO_DR(p)    (0x40040000u + (p) * 0x100u)
#define GPIO_PS(p)    (0x40040004u + (p) * 0x100u)
#define GPIO_PC(p)    (0x40040008u + (p) * 0x100u)
#define DM_HIZ    1u   /* digital input */
#define DM_STRONG 6u   /* push-pull output */

static void pinSetup(unsigned port, unsigned pin, unsigned hsiom, unsigned dm) {
  REG32(HSIOM_SEL(port)) = (REG32(HSIOM_SEL(port)) & ~(0xFu << (pin * 4))) | (hsiom << (pin * 4));
  REG32(GPIO_PC(port))   = (REG32(GPIO_PC(port))   & ~(0x7u << (pin * 3))) | (dm << (pin * 3));
}
static void pinWrite(unsigned port, unsigned pin, int v) {
  if (v) REG32(GPIO_DR(port)) |= (1u << pin);
  else   REG32(GPIO_DR(port)) &= ~(1u << pin);
}
static int pinRead(unsigned port, unsigned pin) { return (REG32(GPIO_PS(port)) >> pin) & 1u; }

void bridgeNss(int level) { pinWrite(4, 3, level); }
int  bridgeBusy(void)     { return pinRead(4, 7); }
int  bridgeDio1(void)     { return pinRead(4, 6); }
void bridgeAntSwitch(int on)    { pinWrite(6, 1, on ? 1 : 0); }
int  bridgeAntSwitchState(void) { return (REG32(GPIO_DR(6)) >> 1) & 1u; }

/* ---- millisecond counter (SysTick, polled; call often) ---- */
static uint32_t msCount;
uint32_t bridgeMs(void) {
  if (REG32(0xE000E010u) & (1u << 16)) msCount++;   /* COUNTFLAG clears on read */
  return msCount;
}
static void delayMs(uint32_t ms) { CyDelay(ms); msCount += ms; }

void bridgeRadioReset(void) {
  pinWrite(5, 7, 0);
  delayMs(2);
  pinWrite(5, 7, 1);
  delayMs(20);
}

/* ---- serial (UART_1 on the Grove port) ---- */
void UART_1_SCB_IRQ_Stop(void);
static void uartBegin(uint32_t baud) {
  UART_1_UartInit();
  UART_1_Enable();
  UART_1_SCB_IRQ_Stop();               /* polled FIFO, like Heltec's Serial */
  UART_1_SCBCLK_DIV_REG = ((uint32_t)((float)CYDEV_BCLK__HFCLK__HZ / baud / UART_1_UART_OVS_FACTOR + 0.5f) - 1u) << 8;
  pinWrite(3, 1, 1);
  pinSetup(3, 0, 9, DM_HIZ);           /* RX: SCB UART function */
  pinSetup(3, 1, 9, DM_STRONG);        /* TX: SCB UART function */
}
int  bridgeUartAvailable(void) { return (int)UART_1_SpiUartGetRxBufferSize(); }
int  bridgeUartRead(void)      { return (int)(UART_1_SpiUartReadRxData() & 0xFFu); }
void bridgeUartWrite(uint8_t b) {
  while (UART_1_SpiUartGetTxBufferSize() >= UART_1_FIFO_SIZE) { }
  UART_1_SpiUartWriteTxData(b);
}

void bridgeBoardInit(uint32_t baud) {
  /* The CubeCell start-up enables the timer interrupt (IRQ 13, CySysTimerIsr),
     whose handler calls pinMode() -> license freeze -> the chip restarts.
     The bridge needs no timer interrupt: switch it off and clear it. */
  REG32(0xE000E180u) = 1u << 13;   /* NVIC ICER */
  REG32(0xE000E280u) = 1u << 13;   /* NVIC ICPR */

  REG32(0xE000E014u) = CYDEV_BCLK__HFCLK__HZ / 1000u - 1u;   /* SysTick 1 ms */
  REG32(0xE000E018u) = 0;
  REG32(0xE000E010u) = 5u;                                   /* core clock, on, no IRQ */

  pinWrite(4, 3, 1); pinSetup(4, 3, 0, DM_STRONG);   /* NSS high */
  pinSetup(4, 6, 0, DM_HIZ);                         /* DIO1 */
  pinSetup(4, 7, 0, DM_HIZ);                         /* BUSY */
  pinWrite(5, 7, 1); pinSetup(5, 7, 0, DM_STRONG);   /* RESET high */
  /* P6.1 powers the Ra-07H's antenna switch (U10 VDD via R26 in Ai-Thinker's
     schematic; ASR's RADIO_ANT_SWITCH_POWER). Heltec's code leaves it off because
     their boards power the switch differently. Without it, TX and RX barely
     reach the antenna. */
  pinWrite(6, 1, 1); pinSetup(6, 1, 0, DM_STRONG);
  SPI_1_Start();
  uartBegin(baud);
  bridgeRadioReset();
}
