
#include <gtest/gtest.h>
#include "chipset.h"

TEST(SerialMouseLive, CuteMouseStyleEnableDeliversPacketAndIrq) {
    ibmpcat::Chipset cs;
    cs.reset();
    cs.pic_master.out(0x20, 0x11);
    cs.pic_master.out(0x21, 0x08);
    cs.pic_master.out(0x21, 0x04);
    cs.pic_master.out(0x21, 0x01);
    cs.pic_master.out(0x21, uint8_t(~(1 << 4)));  // unmask IR4 only

    auto& u = cs.com1;
    u.out(0x3FB, 0x80);
    u.out(0x3F8, 96);
    u.out(0x3F9, 0);
    u.out(0x3FB, 0x02);
    u.out(0x3FC, 0x0F);
    (void)u.in(0x3F8);
    u.out(0x3F9, 0x01);

    uint64_t t = 0;
    cs.tick(t, 8e6);  // sees DTR, queues 'M'
    t += 60000; cs.tick(t, 8e6);
    ASSERT_EQ(u.in(0x3FD) & 1, 1);
    EXPECT_EQ(u.in(0x3F8), 0x4D);

    cs.serial_mouse.inject(40, 0, 0);
    uint8_t pkt[3];
    for (int i = 0; i < 3; ++i) {
        t += 60000; cs.tick(t, 8e6);
        ASSERT_EQ(u.in(0x3FD) & 1, 1) << "byte " << i;
        EXPECT_TRUE(u.irq_pending());
        EXPECT_TRUE(cs.pic_master.has_interrupt()) << "byte " << i;
        pkt[i] = u.in(0x3F8);
        // Line drops after read once tick resamples
        t += 1; cs.tick(t, 8e6);
        EXPECT_FALSE(u.irq_pending());
    }
    EXPECT_EQ(pkt[0] & 0x40, 0x40);
    EXPECT_EQ(pkt[1], 40);
    EXPECT_EQ(pkt[2], 0);
}
