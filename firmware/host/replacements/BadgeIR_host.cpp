// Host BadgeIR: IR idle. No frame is ever sent or received. Same header as
// ir/BadgeIR.cpp; the pure nec_mw_* headers it includes stay on the include path.

#include "ir/BadgeIR.h"

volatile bool irHardwareEnabled = false;
volatile bool pythonIrListening = false;
IrPythonFrame irPythonQueue[IR_PYTHON_QUEUE_SIZE];
volatile int irPythonQueueHead = 0;
volatile int irPythonQueueTail = 0;
portMUX_TYPE irPythonQueueMux = portMUX_INITIALIZER_UNLOCKED;

namespace BadgeIR {
bool sendFrame(const uint32_t*, size_t) { return false; }
bool sendFrameNoWait(const uint32_t*, size_t) { return false; }
// A wait for a frame that will never come still lets time pass.
bool recvFrame(nec_mw_result_t*, uint32_t timeout_ms) {
  delay(timeout_ms);
  return false;
}
}  // namespace BadgeIR

bool irHwIsUp() { return false; }
int irSendRaw(uint8_t, uint8_t) { return -1; }
int irSendWords(const uint32_t*, size_t) { return -1; }
int irReadWords(uint32_t*, size_t, size_t*) { return -1; }
void irDrainPythonRx() {}
int irSetTxPower(int) { return -1; }
int irGetTxPower() { return 0; }
int irSetMode(int mode) { return mode == IR_MODE_BADGE_MW ? 0 : -1; }
int irGetMode() { return IR_MODE_BADGE_MW; }
int irNecSend(uint8_t, uint8_t, uint8_t) { return -1; }
int irNecRead(uint8_t*, uint8_t*, uint8_t*) { return -1; }
int irRawCapture(uint16_t*, size_t) { return 0; }
int irRawSend(const uint16_t*, size_t, uint32_t) { return -1; }
void irDrainAltRx() {}
uint32_t irMsSinceTx() { return UINT32_MAX; }
uint32_t irMsSinceRx() { return UINT32_MAX; }
void irTask(void*) {}
