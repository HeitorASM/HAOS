#include "power.h"
#include "acpi.h"
#include "../fs/vfs.h"

void power_shutdown(void) {
    vfs_sync();          // flush do HAOSFS (journal + FLUSH CACHE) antes de cortar a energia
    acpi_poweroff();
}

void power_reboot(void) {
    vfs_sync();
    acpi_reboot();
}
