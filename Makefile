# XSWL-C — Makefile
#   make          — gcc -O3 (默认)
#   make gui      — gcc -O3 + SDL2 GUI
#   make dev      — clang + sanitizers + trace
#   make release  — gcc + g++ 链接 -O3 -flto

TARGET    = xswl
INC       = -Isrc -I/usr/include -I/usr/include/SDL2
LIBS_TUI  = -lunicorn -lm
LIBS_GUI  = -lunicorn -lSDL2 -lSDL2_image -lm
TEST_ELF  = test_file.elf
GUI_EVENT_TEST_ELF = test_gui_events.elf

SRC_TUI   = src/main.c src/xj380_emu.c
SRC_GUI   = src/main.c src/xj380_emu.c src/xj380_gui.c

# === 默认: gcc -O3 (TUI only) ===
PROD_CC    = gcc
PROD_FLAGS = -std=c11 -pedantic -Wall -Wextra -Wpedantic \
             -Wconversion -Wshadow -Wcast-align \
             -Wstrict-prototypes -Wmissing-prototypes \
             -Wold-style-definition -Wno-pointer-to-int-cast \
             -O3 -DNDEBUG

$(TARGET): $(SRC_TUI) src/xj380_emu.h
	$(PROD_CC) $(PROD_FLAGS) $(INC) -c src/main.c -o main.o
	$(PROD_CC) $(PROD_FLAGS) $(INC) -c src/xj380_emu.c -o xj380_emu.o
	$(PROD_CC) $(PROD_FLAGS) -o $@ main.o xj380_emu.o $(LIBS_TUI)

# === GUI: gcc -O3 + SDL2 ===
GUI_FLAGS  = $(PROD_FLAGS) -DXJ380_GUI -Wno-pedantic

.PHONY: gui
gui:
	$(PROD_CC) $(GUI_FLAGS) $(INC) -c src/main.c -o main.o
	$(PROD_CC) $(GUI_FLAGS) $(INC) -c src/xj380_emu.c -o xj380_emu.o
	$(PROD_CC) $(GUI_FLAGS) $(INC) -c src/xj380_gui.c -o xj380_gui.o
	$(PROD_CC) $(GUI_FLAGS) -o $(TARGET) main.o xj380_emu.o xj380_gui.o $(LIBS_GUI)

# === 开发: clang + sanitizers + trace ===
DEV_FLAGS  = -std=c11 -pedantic -Wall -Wextra -Wpedantic \
             -Wconversion -Wshadow -Wcast-align \
             -Wstrict-prototypes -Wmissing-prototypes \
             -Wold-style-definition -Wno-pointer-to-int-cast \
             -DDEBUG_TRACE -g -O0 \
             -fsanitize=address,undefined -fno-omit-frame-pointer

.PHONY: dev
dev:
	clang $(DEV_FLAGS) $(INC) -c src/main.c -o main.dev.o
	clang $(DEV_FLAGS) $(INC) -c src/xj380_emu.c -o xj380_emu.dev.o
	clang $(DEV_FLAGS) -o $(TARGET) main.dev.o xj380_emu.dev.o $(LIBS_TUI) -fsanitize=address,undefined
	rm -f *.dev.o

# === Release: gcc + g++ 链接 -O3 -flto ===
REL_FLAGS  = -std=c11 -O3 -flto -march=native \
             -funroll-loops -fomit-frame-pointer -DNDEBUG

.PHONY: release
release:
	gcc $(REL_FLAGS) $(INC) -c src/main.c -o main.rel.o
	gcc $(REL_FLAGS) $(INC) -c src/xj380_emu.c -o xj380_emu.rel.o
	g++ $(REL_FLAGS) -o $(TARGET) main.rel.o xj380_emu.rel.o $(LIBS_TUI) -flto
	rm -f *.rel.o

# === 测试 ===
$(TEST_ELF): test_file.S
	gcc -m64 -nostdlib -static -no-pie -o $@ $< -Wl,-Ttext=0x200000

$(GUI_EVENT_TEST_ELF): test_gui_events.S
	gcc -m64 -nostdlib -static -no-pie -o $@ $< -Wl,-Ttext=0x200000

.PHONY: test test-gui-events test-real
test: $(TARGET) $(TEST_ELF)
	./$(TARGET) $(TEST_ELF)

test-gui-events: gui $(GUI_EVENT_TEST_ELF)
	SDL_VIDEODRIVER=dummy XSWL_TEST_GUI_EVENTS=1 ./$(TARGET) $(GUI_EVENT_TEST_ELF)

test-real: $(TARGET)
	./$(TARGET) /home/bnear8273/Develop/Projects/XSWL/main.epf

.PHONY: clean
clean:
	rm -f $(TARGET) $(TEST_ELF) $(GUI_EVENT_TEST_ELF) *.o *.dev.o *.rel.o
