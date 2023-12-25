mkf_abpath	:= $(patsubst %Makefile, %, $(abspath $(MAKEFILE_LIST)))

all:
	@echo --- make server ---
	@echo --- make client ---
	@echo --- make forward ---
	@echo --- make key_gen ---

%:
	mkdir -p bin/
	gcc -o bin/proxy_$* src/$*_proxy.c

key_gen:
	mkdir -p bin/
	gcc src/key_gen.c -o bin/key_gen && ./bin/key_gen 0

# 伪目标
.PHONY: clean

clean:
	rm -rf target
	rm -rf bin

help:



