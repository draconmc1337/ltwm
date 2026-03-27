CC      = gcc
CFLAGS  = -std=c99 -Wall -Wextra -O2 -g -I./include \
          $(shell pkg-config --cflags x11 xrandr xft 2>/dev/null)
LDFLAGS = $(shell pkg-config --libs x11 xrandr xft 2>/dev/null) -lm

TARGET   = ltwm
CLIENT   = ltwmc

WM_SRCS  = src/wm.c src/events.c src/client.c src/workspace.c src/bar.c \
           src/ipc.c src/spawn.c src/config.c src/util.c
WM_OBJS  = $(WM_SRCS:.c=.o)

CLI_SRC  = src/ltwmc.c
CLI_OBJ  = src/ltwmc.o

PREFIX   = /usr/local
BINDIR   = $(PREFIX)/bin
CFGDIR   = $(HOME)/.config/ltwm
XSESSDIR = /usr/share/xsessions

.PHONY: all clean install install-config uninstall

all: $(TARGET) $(CLIENT)

$(TARGET): $(WM_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

$(CLIENT): $(CLI_OBJ)
	$(CC) -o $@ $^

src/%.o: src/%.c include/ltwm.h
	$(CC) $(CFLAGS) -c -o $@ $<

src/ltwmc.o: src/ltwmc.c
	$(CC) -std=c99 -Wall -O2 -c -o $@ $<

install: all
	install -Dm755 $(TARGET)    $(BINDIR)/$(TARGET)
	install -Dm755 $(CLIENT)    $(BINDIR)/$(CLIENT)
	install -Dm755 ltwm.session $(BINDIR)/ltwm-session
	install -Dm644 ltwm.desktop $(XSESSDIR)/ltwm.desktop

install-config:
	mkdir -p $(CFGDIR)
	cp -n ltwmrc $(CFGDIR)/ltwmrc || true
	chmod +x $(CFGDIR)/ltwmrc || true

uninstall:
	rm -f $(BINDIR)/$(TARGET)
	rm -f $(BINDIR)/$(CLIENT)
	rm -f $(BINDIR)/ltwm-session
	rm -f $(XSESSDIR)/ltwm.desktop

clean:
	rm -f $(WM_OBJS) $(CLI_OBJ) $(TARGET) $(CLIENT)
