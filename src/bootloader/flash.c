/**
 * @file flash.c
 * @brief Simulated flash memory — init, erase, write, read, and address
 *        validation.
 *
 * Design notes:
 * - All addresses are logical offsets within the flat UdsFlashMemory.data
 *   array; no hardware mapping is performed.
 * - No dynamic memory; all state lives in the caller-supplied UdsFlashMemory.
 * - Write protection is enforced on erase and write; reads are unrestricted.
 */

#include "uds_flash.h"

#include <string.h>

/* ── Internal helpers ───────────────────────────────────────────────────── */

/**
 * Return the region containing [address, address+length) or NULL if none.
 * A match requires the range to lie entirely within a single region.
 */
static const UdsFlashRegion *find_region(const UdsFlashMemory *flash,
                                          uint32_t address, uint32_t length) {
    for (size_t i = 0U; i < flash->region_count; i++) {
        const UdsFlashRegion *r = &flash->regions[i];
        if (address >= r->base_address &&
            (address - r->base_address) < r->size &&
            length <= r->size - (address - r->base_address)) {
            return r;
        }
    }
    return NULL;
}

/* ── Flash lifecycle ────────────────────────────────────────────────────── */

void uds_flash_init(UdsFlashMemory *flash, const UdsFlashRegion *regions,
                    size_t region_count) {
    if (!flash) {
        return;
    }

    memset(flash->data, UDS_FLASH_ERASE_VALUE, UDS_FLASH_MAX_SIZE);

    if (region_count > UDS_FLASH_MAX_REGIONS) {
        region_count = UDS_FLASH_MAX_REGIONS;
    }

    flash->region_count = region_count;
    if (regions && region_count > 0U) {
        memcpy(flash->regions, regions,
               region_count * sizeof(UdsFlashRegion));
    }

    flash->initialized = true;
}

/* ── Address validation ─────────────────────────────────────────────────── */

bool uds_flash_address_valid(const UdsFlashMemory *flash, uint32_t address,
                              uint32_t length) {
    if (!flash || length == 0U) {
        return false;
    }
    return find_region(flash, address, length) != NULL;
}

/* ── Flash operations ───────────────────────────────────────────────────── */

int uds_flash_erase(UdsFlashMemory *flash, uint32_t address, uint32_t length) {
    if (!flash || length == 0U) {
        return UDS_CORE_ERR_PARAM;
    }

    const UdsFlashRegion *r = find_region(flash, address, length);
    if (!r || r->protected) {
        return UDS_CORE_ERR_NRC;
    }

    /* address is a logical offset directly into data[] */
    memset(&flash->data[address], UDS_FLASH_ERASE_VALUE, length);
    return UDS_CORE_OK;
}

int uds_flash_write(UdsFlashMemory *flash, uint32_t address,
                    const uint8_t *data, size_t len) {
    if (!flash || !data || len == 0U) {
        return UDS_CORE_ERR_PARAM;
    }

    const UdsFlashRegion *r = find_region(flash, address, (uint32_t)len);
    if (!r || r->protected) {
        return UDS_CORE_ERR_NRC;
    }

    memcpy(&flash->data[address], data, len);
    return UDS_CORE_OK;
}

int uds_flash_read(const UdsFlashMemory *flash, uint32_t address,
                   uint8_t *buf, size_t len) {
    if (!flash || !buf || len == 0U) {
        return UDS_CORE_ERR_PARAM;
    }

    if (!uds_flash_address_valid(flash, address, (uint32_t)len)) {
        return UDS_CORE_ERR_NRC;
    }

    memcpy(buf, &flash->data[address], len);
    return UDS_CORE_OK;
}
