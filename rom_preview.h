/*
 * rom_preview.h
 * ROM Preview Image System for MiSTer
 *
 * Handles loading and fetching preview images for ROMs,
 * including downloading from online APIs when internet is available.
 */

#ifndef ROM_PREVIEW_H
#define ROM_PREVIEW_H

#include <inttypes.h>
#include "rom_catalog.h"

// Preview image configuration
#define PREVIEW_WIDTH       256
#define PREVIEW_HEIGHT      192
#define PREVIEW_CACHE_DIR   "previews"
#define PREVIEW_FETCH_TIMEOUT 10  // seconds

// Preview fetch status
typedef enum {
    PREVIEW_STATUS_NONE,
    PREVIEW_STATUS_LOADING,
    PREVIEW_STATUS_READY,
    PREVIEW_STATUS_NOT_FOUND,
    PREVIEW_STATUS_ERROR,
    PREVIEW_STATUS_NO_INTERNET
} preview_status_t;

// Preview data structure
typedef struct {
    preview_status_t status;
    void *image_data;            // Loaded image data (Imlib2 image or raw buffer)
    int width;
    int height;
    char rom_name[256];          // Name of ROM this preview is for
    uint32_t station_id;
} rom_preview_data_t;

// Current preview state
extern rom_preview_data_t g_current_preview;

// Initialize/cleanup
int  rom_preview_init(void);
void rom_preview_cleanup(void);

// Check if internet is available
int  rom_preview_check_internet(void);

// Preview loading (local files)
int  rom_preview_load_local(rom_entry_t *rom);

// Preview fetching (online APIs)
// Supported APIs: libretro-thumbnails, screenscraper, etc.
int  rom_preview_fetch_online(rom_entry_t *rom);

// Async preview fetch (non-blocking)
int  rom_preview_fetch_async(rom_entry_t *rom);
int  rom_preview_fetch_poll(void);  // Returns 1 when complete

// Display the current preview on framebuffer
void rom_preview_display(int x, int y, int max_width, int max_height);

// Display a text placeholder when no preview available
void rom_preview_display_text(int x, int y, const char *rom_name, const char *station_name);

// Clear current preview
void rom_preview_clear(void);

// Get preview status
preview_status_t rom_preview_get_status(void);

// Cache management
int  rom_preview_cache_exists(rom_entry_t *rom);
int  rom_preview_cache_save(rom_entry_t *rom, void *data, int width, int height);
void rom_preview_cache_clear(void);
void rom_preview_cache_clear_station(uint32_t station_id);

// Batch download previews for a station
typedef void (*preview_progress_cb)(int current, int total, const char *rom_name);
int  rom_preview_batch_fetch(uint32_t station_id, preview_progress_cb progress_cb);
void rom_preview_batch_cancel(void);

#endif // ROM_PREVIEW_H
