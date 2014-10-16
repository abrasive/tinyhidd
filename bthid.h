#include <stdio.h>
#include <btstack/run_loop.h>
#include <btstack/utils.h>

typedef struct {
    // used in linked list. so, this must be first
    linked_item_t item;

    // are we trying to establish this?
    int outgoing;
    int outgoing_retries;

    bd_addr_t addr;
    uint16_t handle;
    // L2CAP local channel numbers for each PSM
    uint16_t cid_interrupt, cid_control;
    // raw HID descriptor
    uint8_t *descriptor;
    int descriptor_len;
    // PNPInformation attributes
    uint16_t vendor_id, product_id, version;
    uint8_t *name;

    // uhid-side
    data_source_t *ds;
} bthid_dev_t;

void bthid_packet_handler (uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
void bthid_report_out(bthid_dev_t *dev, uint8_t *report, int size);

// run loop handlers only get told ds, have to seek
bthid_dev_t * bthid_dev_for_ds(data_source_t *ds);
