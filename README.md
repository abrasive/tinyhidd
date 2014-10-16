tinyhidd
========

A minimalist daemon for handling Bluetooth HID devices on Linux.

tinyhidd requires that the [BTstack](http://code.google.com/p/btstack/) daemon
be running to provide access to BT hardware.

tinyhidd is WORKSFORME-ware. Please don't expect support.

Installing
----------

### Setting up BTstack

BTstack talks directly to your Bluetooth interface over libusb. You do not
need kernel Bluetooth support, BlueZ, or DBus (gick).

BTstack should be built in POSIX daemon mode. You will need to configure it
with the USB VID/PID of your BT interface, assuming you are using a USB
interface.
You may need to supply `CFLAGS=-fPIC` during configure.

Make sure that no Bluetooth drivers are loaded (btusb or otherwise), and BlueZ
is not running.

At the time of writing, BTstack does not ship with a persistent database of
link keys; this means that pairings will not persist across a daemon restart.

### Building tinyhidd

Edit the `Makefile` and set the path to the BTstack build directory. Run `make`. 

Your kernel must be built with `CONFIG_UHID`.

### Running tinyhidd

There are no arguments. Suitable connections will automatically be presented
to the Linux UHID subsystem.

### Pairing devices

Not currently implemented...
