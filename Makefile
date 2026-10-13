# cfe — C frontend for mini-asm

CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Iinclude -I$(MINI_ASM_DIR)/include
LDFLAGS  ?=

# Where mini-asm is installed. Override on the command line, e.g.:
#   make MINI_ASM_DIR=$HOME/Downloads/cc-backend
MINI_ASM_DIR ?= ../cc-backend

# Where the prebuilt libmini-asm.a lives (inside this repo)
LIB_DIR   := lib
MINI_LIB  := $(LIB_DIR)/libmini-asm.a

SRCS := src/lexer.cpp \
        src/parser.cpp \
        src/ast_dump.cpp \
        src/codegen.cpp \
        src/main.cpp

BIN  := cfe

.PHONY: all clean test

all: $(BIN)

$(BIN): $(SRCS) $(MINI_LIB)
	$(CXX) $(CXXFLAGS) $(SRCS) $(MINI_LIB) $(LDFLAGS) -o $@

$(MINI_LIB):
	@echo "error: $(MINI_LIB) not found."
	@echo ""
	@echo "Build mini-asm first, then copy the archive into lib/:"
	@echo "    cd <mini-asm-checkout> && make lib"
	@echo "    mkdir -p $(LIB_DIR) && cp libmini-asm.a $(MINI_LIB)"
	@exit 1

clean:
	rm -f $(BIN)
	rm -f /tmp/cfe_out.s /tmp/cfe_out.o /tmp/cfe_bin

test: $(BIN)
	@echo "int main() { return 42; }" > /tmp/cfe_t.c
	@./$(BIN) /tmp/cfe_t.c ; \
	    rc=$$? ; \
	    if [ $$rc -eq 42 ]; then echo "PASS"; else echo "FAIL (got $$rc)"; exit 1; fi

install: $(BIN)
	install -d $(DESTDIR)/usr/local/bin
	install -m 755 $(BIN) $(DESTDIR)/usr/local/bin/$(BIN)

uninstall:
	rm -f $(DESTDIR)/usr/local/bin/$(BIN)
