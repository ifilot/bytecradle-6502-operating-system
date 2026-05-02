PROG = MANDEL.COM
PATCH = ../common/patch_com_header.py
ABI_MAJOR = 1
ABI_MINOR = 1
GEN_HEADER = ../.generated/io.h
JT_INC = ../../src/jumptable.inc

all: 	$(PROG)

$(PROG): ../common/prog.cfg crt0.o main.o prog.lib
	ar65 a prog.lib crt0.o
	ld65 -C ../common/prog.cfg -m main.map main.o -o $(PROG) prog.lib
	python3 $(PATCH) $(PROG) --abi-major $(ABI_MAJOR) --abi-minor $(ABI_MINOR)

crt0.o: ../common/crt0.s $(JT_INC)
	ca65 --cpu 65c02 ../common/crt0.s -o crt0.o

main.s: main.c io.h $(GEN_HEADER)
	cc65 -t none -O --cpu 65c02 main.c

prog.lib: crt0.o
	cp -v /usr/share/cc65/lib/supervision.lib prog.lib 
	ar65 a prog.lib crt0.o

main.o: main.s
	ca65 --cpu 65c02 main.s

clean:
	rm -vf *.o *.lib main.s $(PROG)
