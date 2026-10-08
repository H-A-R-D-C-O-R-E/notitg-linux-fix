CC = i686-w64-mingw32-gcc
CFLAGS = -O2 -Wall
LDFLAGS = -static -static-libgcc -shared -lkernel32

all: winmm.dll

winmm.dll: winmm.c winmm.def
	$(CC) $(CFLAGS) -o $@ winmm.c winmm.def $(LDFLAGS)

install: winmm.dll
	cp winmm.dll ../NotITG-v4.9.1-QuickStart/Program/winmm.dll

clean:
	rm -f winmm.dll
