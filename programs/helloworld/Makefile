PROG = HELLO.COM
OBJ  = helloworld.o
CFG  = rom.cfg
PATCH = ../common/patch_com_header.py
ABI_MAJOR = 1
ABI_MINOR = 0

all: $(PROG)

$(OBJ): helloworld.asm
	ca65 helloworld.asm -o $(OBJ)

$(PROG): $(OBJ)
	ld65 -o $(PROG) -C $(CFG) $(OBJ)
	python3 $(PATCH) $(PROG) --abi-major $(ABI_MAJOR) --abi-minor $(ABI_MINOR)

clean:
	rm -vf $(OBJ) $(PROG)

.PHONY: all clean
