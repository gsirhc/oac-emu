// Microsoft serial mouse on an NS16450 (COM1). 1200 baud, 7 data bits, no
// parity, 1 stop. After DTR/RTS rise it sends 'M', then 3-byte packets:
//   byte0: 01 LB RB Y7 Y6 X7 X6
//   byte1: 0  X5 X4 X3 X2 X1 X0
//   byte2: 0  Y5 Y4 Y3 Y2 Y1 Y0
// X positive is right, Y positive is down (CuteMouse PROTOCOL.TXT Microsoft mode).
// Reports are paced by the UART (~40/s); inject() accumulates while a packet
// is still queued so a fast host pointer does not pile up old motion.
#ifndef IBMPCAT_SERIAL_MOUSE_H
#define IBMPCAT_SERIAL_MOUSE_H

#include "../shared/pc/ns16450.h"

#include <cstdint>

namespace ibmpcat {

class SerialMouse {
public:
    static constexpr uint8_t kLeft = 0x01;
    static constexpr uint8_t kRight = 0x02;
    static constexpr uint8_t kMiddle = 0x04;  // not in the 3-byte packet; ignored

    explicit SerialMouse(pc::Ns16450 &uart) : uart_(uart) {}

    void reset();

    // Watch DTR/RTS; send identification; flush a pending report when the UART can take it.
    void tick();

    // Relative motion and button mask (kLeft/kRight). Clamped per report to ±127.
    void inject(int dx, int dy, uint8_t buttons);

    bool powered() const { return powered_; }

private:
    void try_send();

    pc::Ns16450 &uart_;
    bool powered_ = false;
    uint8_t buttons_ = 0;
    uint8_t sent_buttons_ = 0;
    int pending_dx_ = 0;
    int pending_dy_ = 0;
};

}  // namespace ibmpcat

#endif  // IBMPCAT_SERIAL_MOUSE_H
