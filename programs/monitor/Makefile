GEN_BCOS := ../../src/generate_bcos_artifacts.py
GEN_MON := generate_monitor_jumptable.py
PATCH := ../common/patch_com_header.py
ABI_MAJOR := 1
ABI_MINOR := 1

PROGNAME = monitor

all: generate $(PROGNAME).bin

generate:
	python3 $(GEN_BCOS)
	python3 $(GEN_MON)

# Compiler and flags
AS      = ca65
LD      = ld65
CC      = cc65

SRCS	:= $(wildcard *.s)
OBJS	:= $(sort $(SRCS:.s=.o) jumptable.o)

%.o: %.s *.inc
	$(AS) $< -o $@

jumptable.o: jumptable.s
	$(AS) $< -o $@

jumptable.s jumptable.inc: | generate

$(OBJS): | generate

$(PROGNAME).bin: $(OBJS) rom.cfg
	$(LD) -o $(PROGNAME).bin $(OBJS) -C rom.cfg
	python3 $(PATCH) $(PROGNAME).bin --abi-major $(ABI_MAJOR) --abi-minor $(ABI_MINOR)

run: $(PROGNAME).bin
	../../emulator/build/bc6502emu \
	-r ../../bcos/bin/os.bin \
	-s ../../emulator/script/sdcard.img \
	-p $(PROGNAME).bin

clean:
	rm -vf *.bin *.o jumptable.inc jumptable.s

.PHONY: all generate run clean
