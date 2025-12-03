/*
 * rom_catalog.h
 * ROM Catalog System for MiSTer
 *
 * Provides data structures and functions for managing a catalog of ROMs
 * across multiple game stations/consoles.
 */

#ifndef ROM_CATALOG_H
#define ROM_CATALOG_H

#include <inttypes.h>
#include <stdbool.h>

// Maximum limits
#define ROM_MAX_STATIONS     32      // Maximum number of game stations
#define ROM_MAX_PER_STATION  4096    // Maximum ROMs per station
#define ROM_MAX_TOTAL        32768   // Maximum total ROMs in catalog
#define ROM_NAME_LEN         256     // Maximum ROM name length
#define ROM_PATH_LEN         1024    // Maximum path length
#define ROM_EXT_LEN          32      // Maximum extension length

// ROM entry structure
typedef struct {
    char name[ROM_NAME_LEN];         // Display name (without extension)
    char filename[ROM_NAME_LEN];     // Actual filename
    char path[ROM_PATH_LEN];         // Full path to ROM file
    uint32_t station_id;             // Which station this ROM belongs to
    uint32_t size;                   // File size in bytes
    uint32_t date;                   // File modification date (Unix timestamp)
    uint8_t has_preview;             // 1 if preview image exists
    char preview_path[ROM_PATH_LEN]; // Path to preview image (if exists)
} rom_entry_t;

// Game station structure (user-configured)
typedef struct {
    uint32_t id;                     // Unique station ID
    char name[ROM_NAME_LEN];         // Display name (e.g., "Nintendo Entertainment System")
    char short_name[32];             // Short name (e.g., "NES")
    char rom_path[ROM_PATH_LEN];     // Path to ROMs folder
    char core_path[ROM_PATH_LEN];    // Path to core RBF file
    char extensions[ROM_EXT_LEN];    // Supported file extensions (e.g., "NES")
    uint8_t enabled;                 // Is this station active?
    uint32_t rom_count;              // Number of ROMs found for this station
} rom_station_t;

// Catalog state
typedef struct {
    rom_station_t stations[ROM_MAX_STATIONS];
    uint32_t station_count;
    rom_entry_t *roms;               // Dynamically allocated array of ROMs
    uint32_t rom_count;              // Total number of ROMs
    uint32_t rom_capacity;           // Allocated capacity
    uint8_t initialized;             // Is catalog initialized?
    uint8_t scanning;                // Currently scanning?
    uint32_t scan_progress;          // Scan progress (0-100)
    char scan_status[256];           // Current scan status message
} rom_catalog_t;

// Predefined station templates for easy setup
typedef struct {
    const char *name;
    const char *short_name;
    const char *default_path;
    const char *core_name;
    const char *extensions;
} rom_station_template_t;

// Global catalog instance
extern rom_catalog_t g_rom_catalog;

// Predefined station templates
extern const rom_station_template_t rom_station_templates[];
extern const int rom_station_template_count;

// Catalog management functions
int  rom_catalog_init(void);
void rom_catalog_free(void);
int  rom_catalog_load(void);
int  rom_catalog_save(void);

// Station management
int  rom_station_add(const char *name, const char *short_name,
                     const char *rom_path, const char *core_path,
                     const char *extensions);
int  rom_station_remove(uint32_t station_id);
int  rom_station_update(uint32_t station_id, rom_station_t *station);
rom_station_t* rom_station_get(uint32_t station_id);
rom_station_t* rom_station_get_by_index(int index);
int  rom_station_count(void);

// ROM scanning
int  rom_scan_station(uint32_t station_id);
int  rom_scan_all(void);
void rom_scan_cancel(void);
int  rom_scan_progress(void);
const char* rom_scan_status(void);

// ROM browsing
int  rom_get_count(void);
int  rom_get_count_for_station(uint32_t station_id);
rom_entry_t* rom_get_by_index(int index);
rom_entry_t* rom_get_by_index_for_station(uint32_t station_id, int index);
rom_entry_t* rom_get_selected(void);

// ROM navigation
void rom_browse_init(int filter_station_id);  // -1 for all stations
void rom_browse_scan(int mode);
void rom_browse_print(void);
void rom_browse_scroll_name(void);
int  rom_browse_select(char *path, char *core_path, char *label);
int  rom_browse_get_selected_index(void);
int  rom_browse_get_first_index(void);
int  rom_browse_available(void);

// Preview handling
int  rom_preview_exists(rom_entry_t *rom);
int  rom_preview_load(rom_entry_t *rom, void **image_data, int *width, int *height);
void rom_preview_free(void *image_data);

// Sorting
typedef enum {
    ROM_SORT_NAME_ASC,
    ROM_SORT_NAME_DESC,
    ROM_SORT_STATION_ASC,
    ROM_SORT_STATION_DESC,
    ROM_SORT_DATE_ASC,
    ROM_SORT_DATE_DESC,
    ROM_SORT_SIZE_ASC,
    ROM_SORT_SIZE_DESC
} rom_sort_mode_t;

void rom_sort(rom_sort_mode_t mode);

// Search/Filter
void rom_filter_set(const char *search_text);
void rom_filter_clear(void);
int  rom_filter_active(void);

// Utility functions
const char* rom_get_station_name(uint32_t station_id);
const char* rom_get_display_name(rom_entry_t *rom);
void rom_format_size(uint32_t size, char *buf, int buf_len);

#endif // ROM_CATALOG_H
