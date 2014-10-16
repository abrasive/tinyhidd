#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <btstack/btstack.h>
#include <btstack/utils.h>
#include <btstack/hci_cmds.h>
#include <btstack/linked_list.h>
#include "hiddevs.h"

// inquiry period (in BT time units of 1.28s)
#define INTERVAL 5

// up to 16
#define PIN_LEN  6

// tracking/ignoring previously seen devs {{{
typedef struct {
    linked_item_t item;
    bd_addr_t addr;
} seen_dev_t;

linked_list_t seen_devs = NULL;

int have_seen(bd_addr_t addr) {
    linked_list_iterator_t it;
    linked_list_iterator_init(&it, &seen_devs);
    while (linked_list_iterator_has_next(&it)) {
        seen_dev_t *dev = (seen_dev_t *)linked_list_iterator_next(&it);
        if (!BD_ADDR_CMP(dev->addr, addr))
            return 1;
    }
    seen_dev_t *dev = malloc(sizeof(seen_dev_t));
    memset(dev, 0, sizeof(seen_dev_t));
    BD_ADDR_COPY(dev->addr, addr);
    linked_list_add(&seen_devs, (linked_item_t *)dev);
    return 0;
}
// }}}

static const char *generate_pin(void) { // {{{
    static char pin_buf[PIN_LEN + 1];

    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        printf("Couldn't open /dev/urandom!\n");
        exit(1);
    }

    int i, n;
    uint8_t rand;
    for (i=0; i<PIN_LEN; i++) {
        rand = 0xff;
        // throw away 250..255 to avoid bias
        while (rand >= 250) {
            n = read(fd, &rand, 1);
            if (n < 1) {
                printf("Couldn't read from /dev/urandom!\n");
                exit(1);
            }
        }
        pin_buf[i] = '0' + (rand % 10);
    }
    pin_buf[i] = '\0';
    return pin_buf;
} // }}}

bd_addr_t remote;
int remote_cid = 0, remote_handle = 0;
link_key_t remote_key;
int have_remote = 0;
int paired = 0;
const char *pin = NULL;

#define BREAK_UNLESS_REMOTE(packet, pos)    \
    if (!have_remote)                       \
        break;                              \
    bt_flip_addr(addr, &packet[pos]);       \
    if (BD_ADDR_CMP(addr, remote))          \
        break;

static void start_pairing(void) {
    printf("Pairing...\n");
    hiddevs_remove(remote);
    bt_send_cmd(&l2cap_create_channel, remote, PSM_HID_INTERRUPT);
}

void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    bd_addr_t addr;
    if (packet_type != HCI_EVENT_PACKET)
        return;

    switch (packet[0]) {
        case BTSTACK_EVENT_STATE:
            if (packet[2] != HCI_STATE_WORKING)
                return;
            if (have_remote) {
                start_pairing();
            } else {
                bt_send_cmd(&hci_inquiry, HCI_INQUIRY_LAP, INTERVAL, 0);
                printf("Scanning...\n");
            }
            break;

        case HCI_EVENT_INQUIRY_RESULT:
        case HCI_EVENT_INQUIRY_RESULT_WITH_RSSI:
            bt_flip_addr(addr, &packet[3]);

            if (have_seen(addr))
                break;

            printf("Found device %s", bd_addr_to_str(addr));

            if (packet[13] != 0x25) {
                printf(" - not a HID device\n");
                break;
            }
            if (have_remote) {
                printf(" - but already pairing\n");
                break;
            }

            have_remote = 1;
            BD_ADDR_COPY(remote, addr);
            printf("\n");
            bt_send_cmd(&hci_inquiry_cancel);
            break;
        
        case HCI_EVENT_INQUIRY_COMPLETE:
            // keep scanning!
            if (!have_remote)
                bt_send_cmd(&hci_inquiry, HCI_INQUIRY_LAP, INTERVAL, 0);
            break;

        case HCI_EVENT_COMMAND_COMPLETE:
            if (COMMAND_COMPLETE_EVENT(packet, hci_inquiry_cancel))
                start_pairing();
            break;

        case HCI_EVENT_PIN_CODE_REQUEST:
            BREAK_UNLESS_REMOTE(packet, 2);
            bt_send_cmd(&hci_pin_code_request_reply, &remote, strlen(pin), pin);
            break;

        case HCI_EVENT_LINK_KEY_REQUEST:
            BREAK_UNLESS_REMOTE(packet, 2);
            printf("If using a keyboard, enter the PIN on the device now.\n");
            bt_send_cmd(&hci_link_key_request_negative_reply, &remote);
            break;

        case HCI_EVENT_LINK_KEY_NOTIFICATION:
            BREAK_UNLESS_REMOTE(packet, 2);
            memcpy(remote_key, &packet[8], LINK_KEY_LEN);
            hiddevs_add(remote, remote_key);
            break;
        
        case HCI_EVENT_CONNECTION_COMPLETE:
            BREAK_UNLESS_REMOTE(packet, 5);
            if (paired) {
                printf("Reconnection complete\n");
                exit(0);
            }

            if (packet[2]) { // failure to establish HCI connection
                printf("Failed to establish HCI connection! Will try again.\n");
                if (!paired)
                    start_pairing();
                else
                    bt_send_cmd(&hci_create_connection, &remote, 0x0000, 0, 0, 0, 0);  // XXX we have no way to find valid packet types
            } else {
                remote_handle = READ_BT_16(packet, 3);
            }
            break;

        case L2CAP_EVENT_CHANNEL_OPENED:
            BREAK_UNLESS_REMOTE(packet, 3);

            uint16_t psm = READ_BT_16(packet, 11);
            if (paired) {   // after pairing, we reopen both channels
                if (psm == PSM_HID_INTERRUPT)
                    bt_send_cmd(&l2cap_create_channel, remote, PSM_HID_CONTROL);
                else if (psm == PSM_HID_CONTROL)
                    exit(0);
                break;
            }

            if (psm != PSM_HID_INTERRUPT)
                break;

            if (packet[2]) {
                printf("Failed to pair (status 0x%02X). Check the PIN - many devices use 0000 or 1234\n", packet[2]);
                if (remote_handle)
                    bt_send_cmd(&hci_disconnect, remote_handle, 0x13);
                exit(1);
            } else {
                printf("Pairing succeeded!\n");
                paired = 1;
                
                // disconnect/reconnect so tinyhidd picks it up, if it's
                // running
                bt_send_cmd(&hci_disconnect, remote_handle, 0x13);
            }
            break;

        case HCI_EVENT_DISCONNECTION_COMPLETE:
            if (remote_handle == READ_BT_16(packet, 3) &&
                paired) {
                bt_send_cmd(&hci_create_connection, &remote, 0x0000, 0, 0, 0, 0);  // XXX we have no way to find valid packet types
                printf("Disconnection complete... reconnecting\n");
            }
            break;
    }
}

void usage(void) {
    printf("Usage: tinyhidd-pair [-a 00:22:44:66:88:aa] [-p 1234]\n"
           "\n"
           "    Pair with a HID device. If no address is specified, the first\n"
           "    discoverable HID device that is found is used.\n"
           "    A PIN will be automatically generated if not specified.\n"
          );
    exit(1);
}

int main(int argc, char **argv){
    run_loop_init(RUN_LOOP_POSIX);
    int err = bt_open();
    if (err)
        return err;

    int c;
    while ((c = getopt(argc, argv, "a:p:")) != -1) {
        switch (c) {
            case 'a':
                // sscan_bd_addr is a bit permissive
                if (sscan_bd_addr(optarg, remote) &&
                    strlen(optarg) == 17)
                    have_remote = 1;
                else
                    usage();
                break;

            case 'p':
                pin = optarg;
                if (strlen(pin) > 16) {
                    printf("Specified PIN too long!\n");
                    exit(1);
                }
                break;

            default:
                usage();
        }
    }

    if (optind < argc)
        usage();

    if (!pin)
        pin = generate_pin();
    printf("Using PIN: %s\n", pin);

    bt_register_packet_handler(packet_handler);
	bt_send_cmd(&btstack_set_power_mode, HCI_POWER_ON);
    run_loop_execute();	
    
    return 0;
}
