#include "deadflash/io.h"

#include <stdio.h>
#include <string.h>

static int tokens_equal(const char a[DF_TOKEN_HEX_CHARS + 1],
                        const char b[DF_TOKEN_HEX_CHARS + 1]) {
    return strcmp(a, b) == 0;
}

int main(void) {
    df_target_info geometry;
    df_target_info descriptor;
    df_target_info serial;
    char geometry_token[DF_TOKEN_HEX_CHARS + 1];
    char duplicate_token[DF_TOKEN_HEX_CHARS + 1];
    char descriptor_token[DF_TOKEN_HEX_CHARS + 1];
    char serial_token[DF_TOKEN_HEX_CHARS + 1];
    char changed_serial_token[DF_TOKEN_HEX_CHARS + 1];

    memset(&geometry, 0, sizeof(geometry));
    (void)snprintf(geometry.path, sizeof(geometry.path), "%s", "/dev/deadflash-test");
    geometry.kind = DF_TARGET_BLOCK_DEVICE;
    geometry.size_bytes = 64u * 1024u * 1024u;
    geometry.logical_sector_size = 512u;
    geometry.physical_sector_size = 4096u;

    df_compute_target_token(&geometry, geometry_token);
    df_compute_target_token(&geometry, duplicate_token);
    if (strlen(geometry_token) != DF_TOKEN_HEX_CHARS) return 1;
    if (!tokens_equal(geometry_token, duplicate_token)) return 2;

    descriptor = geometry;
    descriptor.descriptor_present = true;
    (void)snprintf(descriptor.bus_type, sizeof(descriptor.bus_type), "%s", "usb");
    (void)snprintf(descriptor.vendor, sizeof(descriptor.vendor), "%s", "DEADBYTE");
    (void)snprintf(descriptor.product, sizeof(descriptor.product), "%s", "FLASH-01");
    (void)snprintf(descriptor.revision, sizeof(descriptor.revision), "%s", "1.0");
    df_compute_target_token(&descriptor, descriptor_token);
    if (tokens_equal(geometry_token, descriptor_token)) return 3;

    serial = descriptor;
    serial.serial_bound = true;
    memset(serial.serial_sha256, 'a', DF_SHA256_HEX_CHARS);
    serial.serial_sha256[DF_SHA256_HEX_CHARS] = '\0';
    df_compute_target_token(&serial, serial_token);
    if (tokens_equal(descriptor_token, serial_token)) return 4;

    serial.serial_sha256[DF_SHA256_HEX_CHARS - 1u] = 'b';
    df_compute_target_token(&serial, changed_serial_token);
    if (tokens_equal(serial_token, changed_serial_token)) return 5;

    return 0;
}
