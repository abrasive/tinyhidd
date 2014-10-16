BTSTACK=../btstack
CFLAGS=-ggdb -O2 -I$(BTSTACK)/include -I$(BTSTACK)
LDFLAGS=$(BTSTACK)/src/libBTstack.a

all: tinyhidd tinyhidd-pair

tinyhidd: tinyhidd.c bthid.c uhid.c hiddevs.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

tinyhidd-pair: tinyhidd-pair.c hiddevs.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -f tinyhidd
