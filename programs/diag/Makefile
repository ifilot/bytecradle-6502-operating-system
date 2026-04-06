PROG = DIAG.COM

all: $(PROG)

$(PROG): prog.cfg crt0.o main.o callmain.o prog.lib
	ar65 a prog.lib crt0.o
	ld65 -C prog.cfg -m main.map main.o callmain.o -o $(PROG) prog.lib

crt0.o: crt0.s
	ca65 --cpu 65c02 crt0.s

main.s: main.c
	cc65 -t none -O --cpu 65c02 main.c

callmain.s: ../common/callmain.c
	cc65 -t none -O --cpu 65c02 ../common/callmain.c -o callmain.s

prog.lib: crt0.o
	cp -v /usr/share/cc65/lib/supervision.lib prog.lib 
	ar65 a prog.lib crt0.o

main.o: main.s
	ca65 --cpu 65c02 main.s

callmain.o: callmain.s
	ca65 --cpu 65c02 callmain.s

clean:
	rm -vf *.o *.lib main.s callmain.s $(PROG)
