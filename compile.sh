gcc aufnehmer.c -pthread -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include -lgio-2.0 -lgobject-2.0 -lglib-2.0  -o aufnehmer -std=c11 -O2 $(pkg-config --cflags --libs gtk+-3.0) -s -Wl,-O1 -Wl,--as-needed -Wl,--hash-style=gnu

