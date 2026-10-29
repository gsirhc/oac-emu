// Drive mechanism events for the front end's drive noise, stamped in CPU cycles.
#ifndef SHARED_PC_DRIVE_SOUND_H
#define SHARED_PC_DRIVE_SOUND_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pc {

struct DriveSoundEvent {
    enum Kind : uint8_t { kMotorOn, kMotorOff, kSeek, kAccess };
    double cycle;
    Kind kind;
    uint8_t drive;
    int steps;      // kSeek: head moves, each `period` cycles apart
    double period;  // kSeek: cycles per move; kAccess: how long the head reads or writes
};

// Pushes nothing until enabled, so a muted page costs the controllers no work.
class DriveSoundLog {
public:
    bool enabled = false;

    void push(double cycle, DriveSoundEvent::Kind kind, int drive, int steps = 0, double period = 0.0) {
        if (!enabled || events_.size() >= kMaxEvents) return;
        events_.push_back({cycle, kind, uint8_t(drive), steps, period});
    }
    std::vector<DriveSoundEvent> drain() {
        std::vector<DriveSoundEvent> out;
        out.swap(events_);
        return out;
    }

private:
    std::vector<DriveSoundEvent> events_;
    static constexpr std::size_t kMaxEvents = 4096;
};

}  // namespace pc

#endif  // SHARED_PC_DRIVE_SOUND_H
