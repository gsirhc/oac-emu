CORE = ..
SHARED = ../../shared
Z80 = ../../shared/cpu
MCS48 = ../../shared/cpu
NAME = dkong
EXPORT_NAME = DkongArcade
SRC = wasm_machine.cpp $(CORE)/machine.cpp $(CORE)/video.cpp $(CORE)/sound.cpp \
      $(Z80)/cpu_z80.cpp $(MCS48)/cpu_mcs48.cpp
INCLUDES = -I$(Z80)
ROM_ARGS = --png ../../assets/dkong-cabinet.png

include $(SHARED)/arcade.mk
