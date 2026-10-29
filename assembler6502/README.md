# 6502 Assembler

My hand-built W65C02S board. It boots straight into Wozmon, with a resident line editor and a two-pass assembler in the ROM.

## Specs

- CPU: WDC W65C02S at 1 MHz
- RAM: 16K, a 62256 decoded at `$0000`–`$3FFF`
- ROM: 32K EEPROM, a 28C256 at `$8000`–`$FFFF`
- VIA: WDC W65C22 at `$6000`
- Serial: WDC W65C51 ACIA at `$5000`, 1.8432 MHz baud clock
- Reset: Dallas DS1813, about 175 ms
- Display: HD44780 LCD on J3, attached so the stock ROM boots
- Firmware: Steve Wozniak's Wozmon, plus the resident editor and assembler

Hardware notes and citations: [CGOAC6502_REVIEW.md](CGOAC6502_REVIEW.md).
