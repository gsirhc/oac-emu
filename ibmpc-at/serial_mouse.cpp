#include "serial_mouse.h"

#include <algorithm>

namespace ibmpcat {

void SerialMouse::reset() {
    powered_ = false;
    buttons_ = 0;
    sent_buttons_ = 0;
    pending_dx_ = 0;
    pending_dy_ = 0;
}

void SerialMouse::tick() {
    bool power = uart_.dtr() || uart_.rts();
    if (power && !powered_) uart_.push_rx(0x4D);  // 'M' after DTR/RTS rise
    powered_ = power;
    try_send();
}

void SerialMouse::inject(int dx, int dy, uint8_t buttons) {
    if (!powered_) return;
    buttons_ = buttons;
    // Keep a running sum; try_send clamps each on-wire report to ±127.
    pending_dx_ += dx;
    pending_dy_ += dy;
    try_send();
}

void SerialMouse::try_send() {
    if (!powered_) return;
    // One report in flight: wait until the UART has taken the previous three bytes.
    if (uart_.rx_queued() != 0) return;
    if (pending_dx_ == 0 && pending_dy_ == 0 && buttons_ == sent_buttons_) return;

    int dx = std::clamp(pending_dx_, -127, 127);
    int dy = std::clamp(pending_dy_, -127, 127);
    pending_dx_ -= dx;
    pending_dy_ -= dy;
    sent_buttons_ = buttons_;

    uint8_t b0 = 0x40;
    if (buttons_ & kLeft) b0 = uint8_t(b0 | 0x20);
    if (buttons_ & kRight) b0 = uint8_t(b0 | 0x10);
    b0 = uint8_t(b0 | ((dx >> 6) & 0x03));
    b0 = uint8_t(b0 | (((dy >> 6) & 0x03) << 2));
    uart_.push_rx(b0);
    uart_.push_rx(uint8_t(dx & 0x3F));
    uart_.push_rx(uint8_t(dy & 0x3F));
}

}  // namespace ibmpcat
