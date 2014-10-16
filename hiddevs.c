#define _GNU_SOURCE // for strcasestr

#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

#include <btstack/btstack.h>
#include <btstack/utils.h>

#include "hiddevs.h"

// XXX should make this a command-line option
#define HIDDEVS_FILE "hiddevs"

int hiddevs_add(bd_addr_t addr, link_key_t key) {
    if (hiddevs_is_hid(addr))
        return 0;

    // contains link keys, so keep secret
    int fd = open(HIDDEVS_FILE, O_WRONLY|O_CREAT|O_APPEND, 0600);
    if (fd < 0) {
        printf("WARNING - could not open " HIDDEVS_FILE " for writing\n");
        return 1;
    }
    int ret = 0;

    const char *str;
    str = bd_addr_to_str(addr);
    if (write(fd, str, strlen(str)) < strlen(str))
        ret = 1;
    
    if (write(fd, " ", 1) < 1)
        ret = 1;

    str = link_key_to_str(key);
    if (write(fd, str, strlen(str)) < strlen(str))
        ret = 1;

    if (write(fd, "\n", 1) < 1)
        ret = 1;
    close(fd);
    return ret;
}

static int check_and_remove(bd_addr_t addr, int remove, link_key_t key) {
    int mode = remove ? O_RDWR : O_RDONLY;
    int fd = open(HIDDEVS_FILE, mode);
    if (fd < 0) {
        // printf("WARNING - could not open " HIDDEVS_FILE "\n");
        return 0;
    }

    char buf[4096];
    int n = read(fd, buf, sizeof(buf));
    if (n == sizeof(buf)) {
        printf("WARNING - hiddevs file is ridiculously big, will be truncated\n");
        n--;
    }
    buf[n] = '\0';

    char *addr_str = bd_addr_to_str(addr);
    char *found = strcasestr(buf, addr_str);

    if (found && key)
        sscan_link_key(found + 3*BD_ADDR_LEN, key);

    if (!remove || !found) {
        close(fd);
        return !!found;
    }

    int ret = 0;

    lseek(fd, 0, SEEK_SET);
    if (write(fd, buf, found - buf) < 0)
        printf("ERROR - couldn't write to " HIDDEVS_FILE "\n");
    while (*found++ != '\n');
    if (write(fd, found, strlen(found)) < 0)
        printf("ERROR - couldn't write to " HIDDEVS_FILE "\n");
    close(fd);

    return 1;
}

int hiddevs_is_hid(bd_addr_t addr) {
    return check_and_remove(addr, 0, NULL);
}

int hiddevs_read_link_key(bd_addr_t addr, link_key_t key) {
    return check_and_remove(addr, 0, key);
}

int hiddevs_remove(bd_addr_t addr) {
    return check_and_remove(addr, 1, NULL);
}

void hiddevs_forall(void (*process)(bd_addr_t)) {
    int fd = open(HIDDEVS_FILE, O_RDONLY);
    if (fd < 0) {
        // printf("WARNING - could not open " HIDDEVS_FILE "\n");
    }

    char buf[4096], *p;
    int n = read(fd, buf, sizeof(buf));
    if (n == sizeof(buf)) {
        printf("WARNING - hiddevs file is ridiculously big, will be truncated\n");
        n--;
    }
    buf[n] = '\0';
    close(fd);

    bd_addr_t addr;
    char *pbuf = buf;
    while (p = strtok(pbuf, " ")) {
        pbuf = 0;

        if (strlen(p) != 17 ||
            !sscan_bd_addr(p, addr)) {
            printf("Malformatted line in %s: \"%s\"\n", HIDDEVS_FILE, p);
            return;
        }
        process(addr);

        // skip key
        p = strtok(NULL, "\n");
    }
}
