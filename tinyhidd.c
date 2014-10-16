#include <btstack/btstack.h>
#include <btstack/run_loop.h>
#include <btstack/hci_cmds.h>
#include "bthid.h"

int main(int argc, char **argv){
    run_loop_init(RUN_LOOP_POSIX);
    int err = bt_open();
    if (err)
        return err;

    bt_register_packet_handler(bthid_packet_handler);
	bt_send_cmd(&btstack_set_power_mode, HCI_POWER_ON);
	bt_send_cmd(&l2cap_register_service, PSM_HID_CONTROL, 250);
	bt_send_cmd(&l2cap_register_service, PSM_HID_INTERRUPT, 250);
    run_loop_execute();	
    
    return 0;
}

