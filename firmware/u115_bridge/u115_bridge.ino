/*
  U115 radio bridge  (M5Stack Unit LoRaWAN915 / Ai-Thinker Ra-07H / ASR6501)

  Turns the U115 into a "dumb" SX1262 radio that a host (the Cardputer Zero
  running meshtasticd) drives over the Grove UART. The host sends raw SX1262
  SPI transactions; this firmware runs them on the internal radio and sends
  back what the radio answered. When the radio raises DIO1 (packet received,
  transmit done, ...), an event frame is sent to the host.

  Build: firmware/build.ps1 (arduino-cli, board CubeCell-Module = same chip).
  Only the CubeCell start-up and the Cypress drivers are used; Heltec's
  Arduino layer (Serial, pinMode, millis, ...) freezes on this chip, see
  bridge_board.c.

  Frame format (both directions):
    SYNC | CMD | LEN_L | LEN_H | PAYLOAD (LEN bytes) | CHK
    SYNC = 0xA5 host->bridge, 0x5A bridge->host
    CHK  = XOR of CMD, LEN_L, LEN_H and every payload byte

  Commands (host -> bridge), reply uses CMD | 0x80:
    0x01 PING      payload: none            reply: "U115BR" + version
    0x02 XFER      payload: MOSI bytes      reply: MISO bytes (same length)
                   (waits for BUSY low, NSS low, clocks bytes, NSS high)
    0x03 RESET     payload: none            reply: none   (hard-resets the SX1262)
    0x04 PINS      payload: none            reply: [BUSY, DIO1]
    0x05 ANTSW     payload: [0/1] or none   reply: [antenna switch power state] (P6.1, on at start)
  Errors: reply CMD 0xFF, payload [failed cmd, code]
    code 1 = bad checksum, 2 = BUSY timeout, 3 = unknown command, 4 = too long
  Events (bridge -> host, unsolicited):
    0xE1 DIO1 went high (payload: none)
    0xE0 bridge started (payload: version)
*/

#include "Arduino.h"
extern "C" {
  #include "spi-board.h"     // SpiInOut
  #include "sx126x-board.h"  // SX126x global (holds the Spi_t object)
  // bridge_board.c
  void     bridgeBoardInit(uint32_t baud);
  void     bridgeRadioReset(void);
  void     bridgeNss(int level);
  int      bridgeBusy(void);
  int      bridgeDio1(void);
  void     bridgeAntSwitch(int on);
  int      bridgeAntSwitchState(void);
  uint32_t bridgeMs(void);
  int      bridgeUartAvailable(void);
  int      bridgeUartRead(void);
  void     bridgeUartWrite(uint8_t b);
}

#define BRIDGE_VERSION   3
#define BRIDGE_BAUD      115200
#define MAX_PAYLOAD      300
#define BUSY_TIMEOUT_MS  100
#define FRAME_IDLE_MS    50

#define SYNC_IN   0xA5
#define SYNC_OUT  0x5A

static uint8_t rxBuf[MAX_PAYLOAD];
static uint8_t txBuf[MAX_PAYLOAD];
static int lastDio1 = 0;

// ---------- frame output ----------
static void sendFrame(uint8_t cmd, const uint8_t *data, uint16_t len) {
  uint8_t chk = cmd ^ (uint8_t)(len & 0xFF) ^ (uint8_t)(len >> 8);
  bridgeUartWrite(SYNC_OUT);
  bridgeUartWrite(cmd);
  bridgeUartWrite((uint8_t)(len & 0xFF));
  bridgeUartWrite((uint8_t)(len >> 8));
  for (uint16_t i = 0; i < len; i++) {
    bridgeUartWrite(data[i]);
    chk ^= data[i];
  }
  bridgeUartWrite(chk);
}

static void sendError(uint8_t failedCmd, uint8_t code) {
  uint8_t p[2] = { failedCmd, code };
  sendFrame(0xFF, p, 2);
}

// ---------- radio helpers ----------
static bool waitBusyLow() {
  uint32_t start = bridgeMs();
  while (bridgeBusy()) {
    if (bridgeMs() - start > BUSY_TIMEOUT_MS) return false;
  }
  return true;
}

// ---------- command handling ----------
static void handleCommand(uint8_t cmd, uint16_t len) {
  switch (cmd) {
    case 0x01: {  // PING
      const uint8_t p[7] = { 'U', '1', '1', '5', 'B', 'R', BRIDGE_VERSION };
      sendFrame(0x81, p, sizeof(p));
      break;
    }
    case 0x02: {  // XFER
      if (!waitBusyLow()) { sendError(cmd, 2); return; }
      bridgeNss(0);
      for (uint16_t i = 0; i < len; i++) {
        txBuf[i] = (uint8_t)SpiInOut(&SX126x.Spi, rxBuf[i]);
      }
      bridgeNss(1);
      sendFrame(0x82, txBuf, len);
      break;
    }
    case 0x03: {  // RESET
      bridgeRadioReset();
      sendFrame(0x83, NULL, 0);
      break;
    }
    case 0x04: {  // PINS
      uint8_t p[2] = { (uint8_t)bridgeBusy(), (uint8_t)bridgeDio1() };
      sendFrame(0x84, p, 2);
      break;
    }
    case 0x05: {  // ANTSW: payload [0/1] switches the antenna-switch power (P6.1); reply [state]
      if (len >= 1) bridgeAntSwitch(rxBuf[0]);
      uint8_t p[1] = { (uint8_t)bridgeAntSwitchState() };
      sendFrame(0x85, p, 1);
      break;
    }
    default:
      sendError(cmd, 3);
  }
}

// ---------- frame input (state machine) ----------
enum { ST_SYNC, ST_CMD, ST_LEN_L, ST_LEN_H, ST_DATA, ST_CHK };
static uint8_t  st = ST_SYNC;
static uint8_t  inCmd, inChk;
static uint16_t inLen, inPos;
static uint32_t lastByteMs;

static void feedByte(uint8_t b) {
  switch (st) {
    case ST_SYNC:  if (b == SYNC_IN) st = ST_CMD; break;
    case ST_CMD:   inCmd = b; inChk = b; st = ST_LEN_L; break;
    case ST_LEN_L: inLen = b; inChk ^= b; st = ST_LEN_H; break;
    case ST_LEN_H:
      inLen |= ((uint16_t)b << 8); inChk ^= b; inPos = 0;
      if (inLen > MAX_PAYLOAD) { sendError(inCmd, 4); st = ST_SYNC; break; }
      st = (inLen == 0) ? ST_CHK : ST_DATA;
      break;
    case ST_DATA:
      rxBuf[inPos++] = b; inChk ^= b;
      if (inPos >= inLen) st = ST_CHK;
      break;
    case ST_CHK:
      st = ST_SYNC;
      if (b != inChk) { sendError(inCmd, 1); break; }
      handleCommand(inCmd, inLen);
      break;
  }
}

// The CubeCell build turns the SWD pins (P3.2 = SWDIO, P3.3 = SWDCLK) into
// plain GPIO at startup, which locks out the programmer. Put them back to the
// SWD function and drive mode the chip itself uses at power-up (read from the
// chip: HSIOM nibble 0xE, drive mode 6), so it can always be reflashed.
#define REG32(a) (*(volatile uint32_t *)(a))
#define HSIOM_PORT_SEL3 0x40020300u
#define GPIO_PRT3_PC    0x40040308u
static void keepSwdEnabled() {
  REG32(HSIOM_PORT_SEL3) = (REG32(HSIOM_PORT_SEL3) & ~0x0000FF00u) | 0x0000EE00u;
  REG32(GPIO_PRT3_PC)    = (REG32(GPIO_PRT3_PC) & ~(0x3Fu << 6)) | (6u << 6) | (6u << 9);
}

// cyfitter_cfg() (called by initialize_psoc, a constructor that runs before
// main) is where the pins get switched off; restore them right after it.
extern "C" void __real_cyfitter_cfg(void);
extern "C" void __wrap_cyfitter_cfg(void) {
  __real_cyfitter_cfg();
  keepSwdEnabled();
}

// main() calls boardInitMcu() before setup(); the build links with
// -Wl,--wrap=boardInitMcu so our own start-up replaces Heltec's.
extern "C" void __wrap_boardInitMcu(void) {
  keepSwdEnabled();
  bridgeBoardInit(BRIDGE_BAUD);
  keepSwdEnabled();
}

void setup() {
  lastDio1 = bridgeDio1();
  const uint8_t v = BRIDGE_VERSION;
  sendFrame(0xE0, &v, 1);
}

void loop() {
  uint32_t now = bridgeMs();
  while (bridgeUartAvailable()) {
    lastByteMs = now;
    feedByte((uint8_t)bridgeUartRead());
  }
  // drop a half-received frame if the host went quiet
  if (st != ST_SYNC && now - lastByteMs > FRAME_IDLE_MS) st = ST_SYNC;

  // DIO1 is polled: report each rising edge
  int d = bridgeDio1();
  if (d && !lastDio1) sendFrame(0xE1, NULL, 0);
  lastDio1 = d;
}
