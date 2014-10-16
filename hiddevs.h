int hiddevs_remove(bd_addr_t addr);
int hiddevs_add(bd_addr_t addr, link_key_t key);
int hiddevs_is_hid(bd_addr_t addr);
int hiddevs_read_link_key(bd_addr_t addr, link_key_t key);
void hiddevs_forall(void (*process)(bd_addr_t));
extern const char *hiddevs_db_file;
