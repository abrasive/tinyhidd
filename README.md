tinyhidd
========

A minimalist daemon for handling Bluetooth HID devices on Linux.

tinyhidd requires that the [BTstack](http://code.google.com/p/btstack/) daemon
be running to provide access to BT hardware.

tinyhidd is WORKSFORME-ware. Please don't expect support.

Many thanks to Mathias Ringwald and the other contributors to BTstack, for a
fantastic piece of software.

Installing
----------

#### Setting up BTstack

BTstack talks directly to your Bluetooth interface over libusb. You do not
need kernel Bluetooth support, BlueZ, or DBus (gick).

BTstack should be built in POSIX daemon mode. You will need to configure it
with the USB VID/PID of your BT interface, assuming you are using a USB
interface.
You may need to supply `CFLAGS=-fPIC` during configure.

Ensure that `REMOTE_DEVICE_DB` is not defined in `btstack-config.h`, as
otherwise tinyhidd will be unable to pair devices.

Make sure that no Bluetooth drivers are loaded (btusb or otherwise), and BlueZ
is not running.

#### Building tinyhidd

Edit the `Makefile` and set the path to the BTstack build directory. Run `make`. 

Your kernel must be built with `CONFIG_UHID`.

#### Running tinyhidd

There are no arguments. Suitable connections will automatically be presented
to the Linux UHID subsystem.

#### Pairing devices

Run tinyhidd-pair. Devices need to be discoverable, or supplied with the `-a`
command line option. You may need to supply a PIN with `-p`; notably, things
like mice will have their own PINs.

Paired devices are stored in a file named `hiddevs` in the current directory.
This can be changed at the top of `hiddevs.c`. This file must be accessible to
both tinyhidd and tinyhidd-pair.

Troubleshooting
---------------

#### Pairing doesn't work.

Try again. If the HCI still has a connection open to the device, you may have
to wait for it to time out.

If a message about wrong PINs appears, then check if you need to be using the -p argument to supply a fixed PIN (and if so, what it is).

## Known issues

### Pairing is inconsistent

tinyhidd-pair can get itself into some unusual states after a successful pairing, to the point of not noticing that it has succeeded in some cases.
It also isn't very good at retrying. You might have to retry yourself a few times.

### SSP is untested

Secure Simple Pairing hasn't been tested, and may not be fully implemented.
Why?
----

Because BlueZ + DBus is a huge blob of stuff, almost all of which is
unnecessary. DBus is where I draw the line on my machines; anything that
requires it, I will not use. But Bluetooth HID is convenient; hence, this
project.

This is also useful on resource-constrained embedded systems, where smaller is
better. One could presumably wire BTstack to a UART-based HCI instead of USB
on suitable systems if desired.
