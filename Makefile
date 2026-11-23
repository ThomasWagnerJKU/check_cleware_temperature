# check_cleware_temp: statically linked, so the binary runs on any x86_64 Linux
# (needs static glibc and libstdc++, e.g. glibc-devel-static on openSUSE, libc6-dev + libstdc++-dev on Debian)
CC      ?= gcc
CXX     ?= g++
CFLAGS  ?= -O2
CXXFLAGS ?= -O2 -Wall
CPPFLAGS += -DCLEWARELINUX -Icleware
LDFLAGS += -static -s
PREFIX  ?= /usr/local
PLUGINDIR ?= $(PREFIX)/lib/nagios/plugins

all: check_cleware_temp

cleware/USBaccessBasic.o: cleware/USBaccessBasic.c cleware/USBaccessBasic.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

cleware/USBaccess.o: cleware/USBaccess.cpp cleware/USBaccess.h cleware/USBaccessBasic.h cleware/USBaccessDevTypes.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -w -c $< -o $@

check_cleware_temp.o: check_cleware_temp.cpp cleware/USBaccess.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

check_cleware_temp: check_cleware_temp.o cleware/USBaccess.o cleware/USBaccessBasic.o
	$(CXX) $(LDFLAGS) -o $@ $^

install: check_cleware_temp
	install -D -m 0755 check_cleware_temp $(DESTDIR)$(PLUGINDIR)/check_cleware_temp

clean:
	rm -f check_cleware_temp *.o cleware/*.o

.PHONY: all install clean
