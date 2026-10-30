// The generated self-test ROM as a RomSet.

#ifndef DKONG_HWTEST_SET_H
#define DKONG_HWTEST_SET_H

#include "hwtest_roms.h"
#include "machine.h"

#include <algorithm>

namespace dkong {

inline RomSet hwtest_set() {
    namespace h = dkong::hwtest;
    RomSet s;
    std::copy(h::program.begin(), h::program.end(), s.program.begin());
    std::copy_n(h::chars.begin(), 0x800, s.video.char_3p.begin());
    std::copy_n(h::chars.begin() + 0x800, 0x800, s.video.char_3n.begin());
    for (int i = 0; i < 4; i++)
        std::copy_n(h::objs.begin() + i * 0x800, 0x800, s.video.obj[i].begin());
    std::copy_n(h::proms.begin(), 256, s.video.prom_2f.begin());
    std::copy_n(h::proms.begin() + 256, 256, s.video.prom_2e.begin());
    std::copy_n(h::proms.begin() + 512, 256, s.video.prom_2n.begin());
    std::copy_n(h::sound.begin(), 0x800, s.sound_prog.begin());
    std::copy_n(h::sound.begin() + 0x800, 0x800, s.sound_data.begin());
    return s;
}

}  // namespace dkong

#endif
