BTSTACK=../btstack
CFLAGS=-ggdb -O2 -I$(BTSTACK)/include -I$(BTSTACK)
LDFLAGS=$(BTSTACK)/src/libBTstack.a

tinyhidd: tinyhidd.c bthid.c uhid.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -f tinyhidd
