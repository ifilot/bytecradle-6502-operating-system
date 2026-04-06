GEN := ../src/generate_bcos_artifacts.py

SUBDIRS := helloworld hello-c fibonacci monitor mandelbrot

.PHONY: all generate clean

all: generate
	@for dir in $(SUBDIRS); do \
		echo "Building in $$dir..."; \
		$(MAKE) -C $$dir || exit 1; \
	done

generate:
	python3 $(GEN)

clean:
	@for dir in $(SUBDIRS); do \
		echo "Cleaning in $$dir..."; \
		$(MAKE) -C $$dir clean || exit 1; \
	done
