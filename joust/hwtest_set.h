// The generated self-test ROM as a RomSet.

#ifndef JOUST_HWTEST_SET_H
#define JOUST_HWTEST_SET_H

#include "hwtest_roms.h"
#include "machine.h"

#include <algorithm>

namespace joust {

inline RomSet hwtest_set() {
    namespace h = joust::hwtest;
    RomSet s;
    std::copy(h::bank.begin(), h::bank.end(), s.bank.begin());
    std::copy(h::fixed.begin(), h::fixed.end(), s.fixed.begin());
    std::copy(h::decoder4.begin(), h::decoder4.end(), s.decoder4.begin());
    std::copy(h::decoder6.begin(), h::decoder6.end(), s.decoder6.begin());
    std::copy(h::sound.begin(), h::sound.end(), s.sound.begin());
    return s;
}

}  // namespace joust

#endif
