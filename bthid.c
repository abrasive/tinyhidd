#include <string.h>
#include <stdlib.h>

#include <btstack/btstack.h>
#include <btstack/utils.h>
#include <btstack/hci_cmds.h>
#include <btstack/linked_list.h>
#include <btstack/sdp_util.h>
#include "bthid.h"
#include "hiddevs.h"

// utility functions (would be good in sdp_util) {{{
static unsigned int de_get_uint(uint8_t *de) {
    if (de_get_element_type(de) != DE_UINT)
        fprintf(stderr, "WARNING: de_get_uint called on non-uint DE\n");

    de_size_t size = de_get_size_type(de);
    int pos = de_get_header_size(de);
    switch (size) {
        case DE_SIZE_8:
            return de[pos];
        case DE_SIZE_16:
            return READ_NET_16(de, pos);
        case DE_SIZE_32:
            return READ_NET_32(de, pos);
        default:
            fprintf(stderr, "WARNING: invalid size type in de_get_uint\n");
            exit(1);
            return -1;
    }
}
// }}}

// bthid_devs and associated utils {{{
linked_list_t bthid_devs = NULL;

static bthid_dev_t * finddev_addr(bd_addr_t addr) {
    linked_list_iterator_t it;
    linked_list_iterator_init(&it, &bthid_devs);
    while (linked_list_iterator_has_next(&it)) {
        bthid_dev_t *dev = (bthid_dev_t *)linked_list_iterator_next(&it);
        if (!BD_ADDR_CMP(dev->addr, addr))
            return dev;
    }
    return NULL;
}
static bthid_dev_t * finddev_handle(uint16_t handle) {
    linked_list_iterator_t it;
    linked_list_iterator_init(&it, &bthid_devs);
    while (linked_list_iterator_has_next(&it)) {
        bthid_dev_t *dev = (bthid_dev_t *)linked_list_iterator_next(&it);
        if (dev->handle == handle)
            return dev;
    }
    return NULL;
}
static bthid_dev_t * finddev_cid(uint16_t cid) {
    linked_list_iterator_t it;
    linked_list_iterator_init(&it, &bthid_devs);
    while (linked_list_iterator_has_next(&it)) {
        bthid_dev_t *dev = (bthid_dev_t *)linked_list_iterator_next(&it);
        if (dev->cid_interrupt == cid || dev->cid_control == cid)
            return dev;
    }
    return NULL;
}
static bthid_dev_t * newdev(bd_addr_t addr, uint16_t handle) {
    bthid_dev_t *dev = malloc(sizeof(bthid_dev_t));
    memset(dev, 0, sizeof(bthid_dev_t));
    BD_ADDR_COPY(dev->addr, addr);
    dev->handle = handle;
    linked_list_add(&bthid_devs, (linked_item_t *)dev);
    return dev;
}
static void deletedev(bthid_dev_t *dev) {
    linked_list_remove(&bthid_devs, (linked_item_t *)dev);
    if (dev->descriptor)
        free(dev->descriptor);
    free(dev);
}
bthid_dev_t * bthid_dev_for_ds(data_source_t *ds) {
    linked_list_iterator_t it;
    linked_list_iterator_init(&it, &bthid_devs);
    while (linked_list_iterator_has_next(&it)) {
        bthid_dev_t *dev = (bthid_dev_t *)linked_list_iterator_next(&it);
        if (dev->ds == ds)
            return dev;
    }
    return NULL;
}
// }}}

// queueing and running outgoing connection attempts {{{

// called to start connecting, and on L2CAP connection result from outgoing conn
static void outgoing_l2cap_open(bthid_dev_t *dev, int status) {
    if (status) {   // give up - XXX close any conns
        if (dev->outgoing_retries++ > 5) {
            printf("Unable to connect to %s\n", bd_addr_to_str(dev->addr));
            deletedev(dev);
        }
        return;
    }

    if (!dev->cid_interrupt) {
        bt_send_cmd(&l2cap_create_channel, dev->addr, PSM_HID_INTERRUPT);
        return;
    }
    if (!dev->cid_control) {
        bt_send_cmd(&l2cap_create_channel, dev->addr, PSM_HID_CONTROL);
        return;
    }

    if (dev->cid_control && dev->cid_interrupt)
        dev->outgoing = 0;  // we're done
}
static void queue_outgoing_conn(bd_addr_t addr) {
    bthid_dev_t *dev = finddev_addr(addr);
    if (dev)
        return;
    dev = newdev(addr, 0);
    dev->outgoing = 1;
    printf("Attempting connection to %s\n", bd_addr_to_str(dev->addr));
    outgoing_l2cap_open(dev, 0);
}
// }}}

// pump and handle SDP attributes like descriptor and IDs {{{

static void read_hid_descriptor(bthid_dev_t *dev, uint8_t *de, int size) {
    // HID report descs: DES { DES[] { UINT class, STRING descr } }
    // remove outer wrap:
    int len = de_get_len(de);
    if (len > size)
        return; // XXX error
    if (de_get_element_type(de) != DE_DES)
        return; // XXX error

    size -= de_get_header_size(de);
    de   += de_get_header_size(de);

    // search each included DE
    while (size) {
        len = de_get_len(de);
        if (len > size)
            return; // XXX error
        if (de_get_element_type(de) != DE_DES)
            return; // XXX error

        uint8_t *class_desc = de + de_get_header_size(de);
        int class = de_get_uint(class_desc);
        uint8_t *hid_desc = class_desc + de_get_len(class_desc);
        int hiddesc_size = de_get_data_size(hid_desc);
        uint8_t *hiddesc = hid_desc + de_get_header_size(hid_desc);

        if (class != 0x22) {
            size -= len;
            de   += len;
            continue;
        }

        if (dev->descriptor)
            free(dev->descriptor);

        dev->descriptor = malloc(hiddesc_size);
        memcpy(dev->descriptor, hiddesc, hiddesc_size);
        dev->descriptor_len = hiddesc_size;
        return;
    }
    printf("No HID report descriptors found.\n");
}

static bthid_dev_t *sdp_query_dev = NULL;
static void sdp_packet_handler(uint8_t *packet, int size) {
    if (!sdp_query_dev) // no active query! what XXX error
        return;

    int attr = READ_BT_16(packet, 3);
    uint8_t *de = packet + 7;
    int de_size = size - 7;

    switch (attr) {
        case 0x0201:
            sdp_query_dev->vendor_id = de_get_uint(de);
            break;
        case 0x0202:
            sdp_query_dev->product_id = de_get_uint(de);
            break;
        case 0x0203:
            sdp_query_dev->version = de_get_uint(de);
            break;
        case 0x0206:    // from a totally different UUID to the other two XXX
            read_hid_descriptor(sdp_query_dev, de, de_size);
            break;
        default:
            printf("Unexpected SDP attribute 0x%X\n", attr);
    }
}

static void sdp_query_attributes(bthid_dev_t *dev, uint16_t uuid, uint16_t first, uint16_t last) {
    if (sdp_query_dev)
        return;
    sdp_query_dev = dev;
    uint8_t ids[10], atts[20];
    de_create_sequence(ids);
    de_add_number(ids, DE_UUID, DE_SIZE_16, uuid);
    de_create_sequence(atts);
    de_add_number(atts, DE_UINT, DE_SIZE_32, (first<<16) | last);
    bt_send_cmd(&sdp_client_query_services, &dev->addr, ids, atts);

}

// while not all desired attributes are known, send more requests -- one at a time
static void pump_attributes(bthid_dev_t *dev) {
    if (!dev->name) {
        bt_send_cmd(&hci_remote_name_request, &dev->addr, 2, 0, 0);
        return;
    }
    if (!dev->descriptor) {
        sdp_query_attributes(dev, 0x1124, 0x0206, 0x0206);   // HID - Descriptors
        return;
    }
    if (!dev->vendor_id || !dev->product_id || !dev->version) {
        sdp_query_attributes(dev, 0x1200, 0x0201, 0x0203);  // PNPInformation - VID, PID, version
        return;
    }

    // we have everything to begin, stop pumping and run
    printf("HID device active\n");
    uhid_register(dev);
}
// }}}

void bthid_report_out(bthid_dev_t *dev, uint8_t *report, int size) {
    uint8_t sndbuf[4097];
    sndbuf[0] = 0xA2;   // DATA | report out
    memcpy(sndbuf+1, report, size);
    bt_send_l2cap(dev->cid_interrupt, sndbuf, size+1);
}

// main packet handler. handles connection state {{{
void bthid_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    bd_addr_t remote;
    bthid_dev_t *dev = NULL;
    int psm, local_cid, handle;

    if (packet_type == SDP_CLIENT_PACKET)
        sdp_packet_handler(packet, size);

    if (packet_type == L2CAP_DATA_PACKET) {
        dev = finddev_cid(channel);
        if (!dev)
            return;
        if (packet[0] == 0xA1)  // DATA | report in
            uhid_report_in(dev, packet+1, size+1);
    }

    if (packet_type == HCI_EVENT_PACKET &&
        packet[0] == SDP_QUERY_COMPLETE) {
        dev = sdp_query_dev;
        sdp_query_dev = NULL;
        pump_attributes(dev);
    }

    if (packet_type != HCI_EVENT_PACKET)
        return;
    switch (packet[0]) {
        case BTSTACK_EVENT_STATE:
            if (packet[2] != HCI_STATE_WORKING)
                return;
            // try and connect to all known devs
            hiddevs_forall(queue_outgoing_conn);
            break;

        case HCI_EVENT_CONNECTION_REQUEST:
            bt_flip_addr(remote, &packet[2]);
            if (!hiddevs_is_hid(remote))
                break;

            dev = finddev_addr(remote);
            if (!dev)
                dev = newdev(remote, 0);
            break;

        case HCI_EVENT_CONNECTION_COMPLETE:
            if (packet[2])  // failure
                break;

            bt_flip_addr(remote, &packet[5]);
            if (!hiddevs_is_hid(remote))
                break;
                
            printf("New connection\n");
            dev = finddev_addr(remote);
            handle = READ_BT_16(packet, 3);

            // if we got an incoming request, dev exists.
            // for outgoing requests, dev should already exist
            if (!dev)   // XXX error
                break;

            dev->handle = handle;
            break;

        case HCI_EVENT_DISCONNECTION_COMPLETE:
            dev = finddev_handle(READ_BT_16(packet, 3));
            if (dev) {
                printf("Disconnected\n");
                uhid_unregister(dev);
                deletedev(dev);
            }
            break;

        case HCI_EVENT_READ_REMOTE_SUPPORTED_FEATURES_COMPLETE:
            handle = READ_BT_16(packet, 3);
            if (dev = finddev_handle(handle))
                bt_send_cmd(&hci_switch_role_command, &dev->addr, 0);  // go to master
            break;

        case BTSTACK_EVENT_REMOTE_NAME_CACHED:
        case HCI_EVENT_REMOTE_NAME_REQUEST_COMPLETE:
            if (packet[2])
                break;
            bt_flip_addr(remote, &packet[3]);
            dev = finddev_addr(remote);
            if (!dev)
                break;
            if (!dev->name)
                dev->name = strdup(packet+9);

            pump_attributes(dev);
            break;

        case L2CAP_EVENT_INCOMING_CONNECTION:
            bt_flip_addr(remote, packet + 2);
            handle = READ_BT_16(packet, 8); 
            psm = READ_BT_16(packet, 10); 
            local_cid = READ_BT_16(packet, 12); 

            if (!hiddevs_is_hid(remote))
                break;

            if (psm != PSM_HID_INTERRUPT &&
                psm != PSM_HID_CONTROL)
                break;

            dev = finddev_addr(remote);
            if (!dev)
                dev = newdev(remote, handle);

            bt_send_cmd(&l2cap_accept_connection, local_cid);
            break;

        case L2CAP_EVENT_CHANNEL_OPENED:
            bt_flip_addr(remote, packet + 3);

            if (!hiddevs_is_hid(remote))
                break;

            dev = finddev_addr(remote);
            if (!dev)   // XXX this is an error
                break;

            handle = READ_BT_16(packet, 9);
            psm = READ_BT_16(packet, 11);
            local_cid = READ_BT_16(packet, 13);
            if (!packet[2]) {
                if (psm == PSM_HID_CONTROL)
                    dev->cid_control = local_cid;
                if (psm == PSM_HID_INTERRUPT)
                    dev->cid_interrupt = local_cid;
            }

            if (dev->outgoing)
                outgoing_l2cap_open(dev, packet[2]);

            if (dev->cid_control && dev->cid_interrupt)
                pump_attributes(dev);

            break;
        
        case HCI_EVENT_LINK_KEY_REQUEST:
            bt_flip_addr(remote, &packet[2]);
            link_key_t key;
            if (!hiddevs_read_link_key(remote, key))
                break;
            bt_send_cmd(&hci_link_key_request_reply, &remote, &key);
            break;
    }
}
// }}}
