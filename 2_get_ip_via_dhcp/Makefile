# binary name
APP = bin/main

# all source are stored in SRCS-y
SRCS-y := src/main.cpp

PKGCONF ?= pkg-config

CFLAGS += -O3 -Wall --std=c++17 $(shell $(PKGCONF) --cflags libdpdk)
LDFLAGS += $(shell $(PKGCONF) --libs libdpdk)

$(APP): $(SRCS-y) Makefile
	mkdir -p bin
	g++ $(CFLAGS) $(SRCS-y) -o $@ $(LDFLAGS)

run: $(APP)
	sudo $(APP)

clean:
	rm -rf bin
