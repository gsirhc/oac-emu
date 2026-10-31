CORE = ..
SHARED = ../../shared
CPU = ../../shared/cpu
WILLIAMS = ../../shared/williams
NAME = joust
EXPORT_NAME = JoustArcade
SRC = wasm_machine.cpp $(CORE)/machine.cpp \
      $(WILLIAMS)/pia6821.cpp $(WILLIAMS)/blitter.cpp $(WILLIAMS)/sound_board.cpp \
      $(CPU)/cpu_m6809.cpp $(CPU)/cpu_m6800.cpp
INCLUDES = -I$(CPU)
ROM_ARGS = --png ../../assets/joust-cabinet.png

include $(SHARED)/arcade.mk
