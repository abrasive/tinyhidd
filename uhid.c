#include <stdint.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <linux/uhid.h>
#include "bthid.h"
#include "uhid.h"

static int uhid_write(int fd, const struct uhid_event *ev) {
	ssize_t ret;

	ret = write(fd, ev, sizeof(*ev));
    return ret != sizeof(*ev);
}

static int process(data_source_t *ds) {
    struct uhid_event ev;
    ssize_t ret;
    ret = read(ds->fd, &ev, sizeof(ev));
    if (ret != sizeof(ev))
        return;

    if (ev.type == UHID_OUTPUT) {
        bthid_dev_t *dev = bthid_dev_for_ds(ds);
        bthid_report_out(dev, ev.u.output.data, ev.u.output.size);
    }
}

static int create(int fd, bthid_dev_t *dev) {
    struct uhid_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = UHID_CREATE;

    strncpy((char*)ev.u.create.name, dev->name, sizeof(ev.u.create.name));
    ev.u.create.vendor = dev->vendor_id;
    ev.u.create.product = dev->product_id;
    ev.u.create.version = dev->version;

    ev.u.create.rd_data = dev->descriptor;
    ev.u.create.rd_size = dev->descriptor_len;
    ev.u.create.bus = BUS_BLUETOOTH;

    snprintf((char*)ev.u.create.phys, sizeof(ev.u.create.phys), "tinyhidd-%s", bd_addr_to_str(dev->addr));
    strncpy((char*)ev.u.create.uniq, bd_addr_to_str(dev->addr), sizeof(ev.u.create.uniq));
    return uhid_write(fd, &ev);
}

void uhid_register(bthid_dev_t *dev) {
    if (dev->ds) {
        printf("ERROR: Tried to register device more than once\n");
        return;
    }
    int fd = open("/dev/uhid", O_RDWR);
    if (fd < 0) {
        printf("ERROR: Cannot open /dev/uhid!\n");
        exit(1);
    }
    int ret = create(fd, dev);
    if (ret) {
        close(fd);
        printf("ERROR: Cannot create UHID device!\n");
        return;
    }

    dev->ds = malloc(sizeof(data_source_t));
    dev->ds->fd = fd;
    dev->ds->process = process;
    run_loop_add_data_source(dev->ds);
}

void uhid_unregister(bthid_dev_t *dev) {
    if (!dev->ds)
        return;
    run_loop_remove_data_source(dev->ds);
    // auto-destroy
    close(dev->ds->fd);
    free(dev->ds);
}

void uhid_report_in(bthid_dev_t *dev, uint8_t *report, int size) {
    if (!dev->ds)
        return;

    struct uhid_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = UHID_INPUT;
    ev.u.input.size = size;
    memcpy(ev.u.input.data, report, size);
    uhid_write(dev->ds->fd, &ev);
}
