/*
 * rom_catalog.cpp
 * ROM Catalog System for MiSTer
 *
 * Implementation of ROM catalog management, scanning, and browsing.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ctype.h>
#include <algorithm>

#include "rom_catalog.h"
#include "file_io.h"
#include "osd.h"
#include "cfg.h"

// Global catalog instance
rom_catalog_t g_rom_catalog = {};

// Predefined station templates for common consoles
// Extensions are space-separated for clarity. The path is relative to /games/
const rom_station_template_t rom_station_templates[] = {
    { "Nintendo Entertainment System", "NES",      "NES",       "NES",       "nes" },
    { "Super Nintendo",                "SNES",     "SNES",      "SNES",      "sfc smc bin" },
    { "Sega Genesis / Mega Drive",     "Genesis",  "Genesis",   "Genesis",   "bin gen md smd" },
    { "Sega Master System",            "SMS",      "SMS",       "SMS",       "sms sg" },
    { "Game Boy",                      "GB",       "GameBoy",   "GAMEBOY",   "gb gbc" },
    { "Game Boy Color",                "GBC",      "GameBoy",   "GAMEBOY",   "gbc gb" },
    { "Game Boy Advance",              "GBA",      "GBA",       "GBA",       "gba" },
    { "Nintendo 64",                   "N64",      "N64",       "N64",       "n64 z64 v64" },
    { "Atari 2600",                    "A2600",    "Atari2600", "ATARI2600", "a26 bin" },
    { "Atari 7800",                    "A7800",    "Atari7800", "ATARI7800", "a78 bin" },
    { "Atari 5200",                    "A5200",    "Atari5200", "ATARI5200", "a52 bin car" },
    { "ColecoVision",                  "Coleco",   "Coleco",    "Coleco",    "col bin rom" },
    { "TurboGrafx-16 / PC Engine",     "TG16",     "TGFX16",    "TGFX16",    "pce bin sgx" },
    { "Neo Geo",                       "NeoGeo",   "NEOGEO",    "NEOGEO",    "neo" },
    { "Arcade",                        "Arcade",   "_Arcade",   "",          "mra" },
    { "PlayStation 1",                 "PS1",      "PSX",       "PSX",       "cue chd bin iso img pbp" },
    { "PlayStation",                   "PSX",      "PSX",       "PSX",       "cue chd bin iso img pbp" },
    { "Sega CD / Mega CD",             "SegaCD",   "MegaCD",    "MegaCD",    "cue chd iso" },
    { "Sega Saturn",                   "Saturn",   "Saturn",    "Saturn",    "cue chd" },
    { "Sega 32X",                      "S32X",     "S32X",      "S32X",      "32x bin" },
    { "Commodore 64",                  "C64",      "C64",       "C64",       "prg crt t64 d64" },
    { "Amiga",                         "Amiga",    "Amiga",     "Minimig",   "adf hdf" },
    { "Atari ST",                      "AtariST",  "AtariST",   "AtariST",   "st stx" },
    { "MSX",                           "MSX",      "MSX",       "MSX",       "rom dsk cas mx1 mx2" },
    { "ZX Spectrum",                   "Spectrum", "Spectrum",  "Spectrum",  "tap tzx z80 dsk trd" },
    { "Amstrad CPC",                   "CPC",      "Amstrad",   "Amstrad",   "dsk cdt cpr" },
    { "Intellivision",                 "Intv",     "Intellivision", "Intellivision", "int bin rom" },
    { "Vectrex",                       "Vectrex", "Vectrex",    "Vectrex",   "vec bin rom" },
    { "WonderSwan",                    "WS",       "WonderSwan", "WonderSwan", "ws wsc" },
    { "Neo Geo Pocket",                "NGP",      "NeoGeo",    "NeoGeo",    "ngp ngc" },
};

const int rom_station_template_count = sizeof(rom_station_templates) / sizeof(rom_station_templates[0]);

// Navigation state (similar to recent.cpp pattern)
static int iSelectedEntry = 0;
static int iFirstEntry = 0;
static int browse_station_filter = -1;  // -1 = all stations
static rom_entry_t **filtered_roms = NULL;
static int filtered_count = 0;
static int filtered_capacity = 0;
static char search_filter[256] = "";
static volatile int scan_cancel_flag = 0;

// Forward declarations
static void rebuild_filtered_list(void);
static int match_extension(const char *filename, const char *extensions);
static void extract_display_name(const char *filename, char *display_name, int max_len);

/*****************************************************************************
 * Catalog Initialization and Cleanup
 *****************************************************************************/

int rom_catalog_init(void)
{
    if (g_rom_catalog.initialized) return 0;

    memset(&g_rom_catalog, 0, sizeof(g_rom_catalog));

    // Initial allocation for ROMs
    g_rom_catalog.rom_capacity = 1024;
    g_rom_catalog.roms = (rom_entry_t*)malloc(g_rom_catalog.rom_capacity * sizeof(rom_entry_t));
    if (!g_rom_catalog.roms) {
        return -1;
    }

    g_rom_catalog.initialized = 1;
    return 0;
}

void rom_catalog_free(void)
{
    if (g_rom_catalog.roms) {
        free(g_rom_catalog.roms);
        g_rom_catalog.roms = NULL;
    }

    if (filtered_roms) {
        free(filtered_roms);
        filtered_roms = NULL;
    }

    memset(&g_rom_catalog, 0, sizeof(g_rom_catalog));
}

// Config file name for catalog
static const char* rom_catalog_config_name(void)
{
    return "rom_catalog.cfg";
}

static const char* rom_stations_config_name(void)
{
    return "rom_stations.cfg";
}

int rom_catalog_load(void)
{
    if (!g_rom_catalog.initialized) {
        if (rom_catalog_init() < 0) return -1;
    }

    // Load stations configuration
    FileLoadConfig(rom_stations_config_name(), g_rom_catalog.stations, sizeof(g_rom_catalog.stations));

    // Count enabled stations
    g_rom_catalog.station_count = 0;
    for (int i = 0; i < ROM_MAX_STATIONS; i++) {
        if (g_rom_catalog.stations[i].enabled) {
            g_rom_catalog.station_count++;
        }
    }

    return 0;
}

int rom_catalog_save(void)
{
    FileSaveConfig(rom_stations_config_name(), g_rom_catalog.stations, sizeof(g_rom_catalog.stations));
    return 0;
}

/*****************************************************************************
 * Station Management
 *****************************************************************************/

int rom_station_add(const char *name, const char *short_name,
                    const char *rom_path, const char *core_path,
                    const char *extensions)
{
    // Find first empty slot
    int slot = -1;
    for (int i = 0; i < ROM_MAX_STATIONS; i++) {
        if (!g_rom_catalog.stations[i].enabled) {
            slot = i;
            break;
        }
    }

    if (slot < 0) return -1;  // No space

    rom_station_t *station = &g_rom_catalog.stations[slot];
    memset(station, 0, sizeof(rom_station_t));

    station->id = slot;
    strncpy(station->name, name, sizeof(station->name) - 1);
    strncpy(station->short_name, short_name, sizeof(station->short_name) - 1);
    strncpy(station->rom_path, rom_path, sizeof(station->rom_path) - 1);
    strncpy(station->core_path, core_path, sizeof(station->core_path) - 1);
    strncpy(station->extensions, extensions, sizeof(station->extensions) - 1);
    station->enabled = 1;

    g_rom_catalog.station_count++;
    rom_catalog_save();

    return slot;
}

int rom_station_remove(uint32_t station_id)
{
    if (station_id >= ROM_MAX_STATIONS) return -1;
    if (!g_rom_catalog.stations[station_id].enabled) return -1;

    // Remove all ROMs for this station
    int write_idx = 0;
    for (uint32_t i = 0; i < g_rom_catalog.rom_count; i++) {
        if (g_rom_catalog.roms[i].station_id != station_id) {
            if (write_idx != (int)i) {
                g_rom_catalog.roms[write_idx] = g_rom_catalog.roms[i];
            }
            write_idx++;
        }
    }
    g_rom_catalog.rom_count = write_idx;

    // Disable station
    g_rom_catalog.stations[station_id].enabled = 0;
    g_rom_catalog.station_count--;

    rom_catalog_save();
    rebuild_filtered_list();

    return 0;
}

int rom_station_update(uint32_t station_id, rom_station_t *station)
{
    if (station_id >= ROM_MAX_STATIONS) return -1;

    memcpy(&g_rom_catalog.stations[station_id], station, sizeof(rom_station_t));
    rom_catalog_save();

    return 0;
}

rom_station_t* rom_station_get(uint32_t station_id)
{
    if (station_id >= ROM_MAX_STATIONS) return NULL;
    if (!g_rom_catalog.stations[station_id].enabled) return NULL;
    return &g_rom_catalog.stations[station_id];
}

rom_station_t* rom_station_get_by_index(int index)
{
    int count = 0;
    for (int i = 0; i < ROM_MAX_STATIONS; i++) {
        if (g_rom_catalog.stations[i].enabled) {
            if (count == index) {
                return &g_rom_catalog.stations[i];
            }
            count++;
        }
    }
    return NULL;
}

int rom_station_count(void)
{
    return g_rom_catalog.station_count;
}

/*****************************************************************************
 * ROM Scanning
 *****************************************************************************/

static int add_rom_entry(rom_station_t *station, const char *path, const char *filename,
                         uint32_t size, uint32_t date)
{
    // Ensure capacity
    if (g_rom_catalog.rom_count >= g_rom_catalog.rom_capacity) {
        uint32_t new_capacity = g_rom_catalog.rom_capacity * 2;
        rom_entry_t *new_roms = (rom_entry_t*)realloc(g_rom_catalog.roms,
                                                       new_capacity * sizeof(rom_entry_t));
        if (!new_roms) return -1;
        g_rom_catalog.roms = new_roms;
        g_rom_catalog.rom_capacity = new_capacity;
    }

    rom_entry_t *rom = &g_rom_catalog.roms[g_rom_catalog.rom_count];
    memset(rom, 0, sizeof(rom_entry_t));

    extract_display_name(filename, rom->name, sizeof(rom->name));
    strncpy(rom->filename, filename, sizeof(rom->filename) - 1);
    strncpy(rom->path, path, sizeof(rom->path) - 1);
    rom->station_id = station->id;
    rom->size = size;
    rom->date = date;

    // Check for preview image
    char preview_path[ROM_PATH_LEN];
    snprintf(preview_path, sizeof(preview_path), "%s/previews/%s.png",
             getFullPath(GAMES_DIR), rom->name);
    if (FileExists(preview_path, 0)) {
        rom->has_preview = 1;
        strncpy(rom->preview_path, preview_path, sizeof(rom->preview_path) - 1);
    } else {
        // Try station-specific preview folder
        snprintf(preview_path, sizeof(preview_path), "%s/%s/previews/%s.png",
                 getFullPath(GAMES_DIR), station->short_name, rom->name);
        if (FileExists(preview_path, 0)) {
            rom->has_preview = 1;
            strncpy(rom->preview_path, preview_path, sizeof(rom->preview_path) - 1);
        }
    }

    g_rom_catalog.rom_count++;
    station->rom_count++;

    return 0;
}

static void scan_directory_recursive(rom_station_t *station, const char *dir_path, int depth)
{
    if (depth > 5 || scan_cancel_flag) return;  // Max recursion depth

    DIR *dir = opendir(dir_path);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && !scan_cancel_flag) {
        if (entry->d_name[0] == '.') continue;

        char full_path[ROM_PATH_LEN];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) < 0) continue;

        if (S_ISDIR(st.st_mode)) {
            // Recurse into subdirectory
            scan_directory_recursive(station, full_path, depth + 1);
        } else if (S_ISREG(st.st_mode)) {
            // Check if file matches extensions
            if (match_extension(entry->d_name, station->extensions)) {
                add_rom_entry(station, full_path, entry->d_name, st.st_size, st.st_mtime);
            }
        }
    }

    closedir(dir);
}

int rom_scan_station(uint32_t station_id)
{
    rom_station_t *station = rom_station_get(station_id);
    if (!station) return -1;

    scan_cancel_flag = 0;
    g_rom_catalog.scanning = 1;
    snprintf(g_rom_catalog.scan_status, sizeof(g_rom_catalog.scan_status),
             "Scanning %s...", station->short_name);

    // Remove existing ROMs for this station first
    int write_idx = 0;
    for (uint32_t i = 0; i < g_rom_catalog.rom_count; i++) {
        if (g_rom_catalog.roms[i].station_id != station_id) {
            if (write_idx != (int)i) {
                g_rom_catalog.roms[write_idx] = g_rom_catalog.roms[i];
            }
            write_idx++;
        }
    }
    g_rom_catalog.rom_count = write_idx;
    station->rom_count = 0;

    // Build full path to ROM directory
    char full_path[ROM_PATH_LEN];

    // Try different storage locations
    const char *storage_dirs[] = {
        getFullPath(GAMES_DIR),  // games/
        getFullPath(""),         // root
        NULL
    };

    for (int i = 0; storage_dirs[i] && !scan_cancel_flag; i++) {
        snprintf(full_path, sizeof(full_path), "%s/%s", storage_dirs[i], station->rom_path);
        if (PathIsDir(full_path, 0)) {
            scan_directory_recursive(station, full_path, 0);
        }
    }

    g_rom_catalog.scanning = 0;
    rebuild_filtered_list();

    return station->rom_count;
}

int rom_scan_all(void)
{
    scan_cancel_flag = 0;
    g_rom_catalog.scanning = 1;
    int total = 0;

    for (int i = 0; i < ROM_MAX_STATIONS && !scan_cancel_flag; i++) {
        if (g_rom_catalog.stations[i].enabled) {
            g_rom_catalog.scan_progress = (i * 100) / g_rom_catalog.station_count;
            total += rom_scan_station(i);
        }
    }

    g_rom_catalog.scanning = 0;
    g_rom_catalog.scan_progress = 100;
    strcpy(g_rom_catalog.scan_status, "Scan complete");

    return total;
}

void rom_scan_cancel(void)
{
    scan_cancel_flag = 1;
}

int rom_scan_progress(void)
{
    return g_rom_catalog.scan_progress;
}

const char* rom_scan_status(void)
{
    return g_rom_catalog.scan_status;
}

/*****************************************************************************
 * ROM Browsing
 *****************************************************************************/

static void rebuild_filtered_list(void)
{
    // Free old list
    if (filtered_roms) {
        free(filtered_roms);
        filtered_roms = NULL;
    }

    filtered_count = 0;
    filtered_capacity = g_rom_catalog.rom_count > 0 ? g_rom_catalog.rom_count : 64;
    filtered_roms = (rom_entry_t**)malloc(filtered_capacity * sizeof(rom_entry_t*));
    if (!filtered_roms) return;

    for (uint32_t i = 0; i < g_rom_catalog.rom_count; i++) {
        rom_entry_t *rom = &g_rom_catalog.roms[i];

        // Filter by station
        if (browse_station_filter >= 0 && (int)rom->station_id != browse_station_filter) {
            continue;
        }

        // Filter by search text
        if (search_filter[0]) {
            if (!strcasestr(rom->name, search_filter)) {
                continue;
            }
        }

        filtered_roms[filtered_count++] = rom;
    }
}

void rom_browse_init(int filter_station_id)
{
    browse_station_filter = filter_station_id;
    iFirstEntry = 0;
    iSelectedEntry = 0;
    search_filter[0] = 0;
    rebuild_filtered_list();
}

int rom_browse_available(void)
{
    return filtered_count;
}

void rom_browse_scan(int mode)
{
    if (mode == SCANF_INIT) {
        iFirstEntry = 0;
        iSelectedEntry = 0;
    } else {
        if (!filtered_count) return;

        int osd_size = OsdGetSize();

        if (mode == SCANF_END || (mode == SCANF_PREV && iSelectedEntry <= 0)) {
            iSelectedEntry = filtered_count - 1;
            iFirstEntry = iSelectedEntry - osd_size + 1;
            if (iFirstEntry < 0) iFirstEntry = 0;
        }
        else if (mode == SCANF_NEXT) {
            if (iSelectedEntry + 1 < filtered_count) {
                iSelectedEntry++;
                if (iSelectedEntry > iFirstEntry + osd_size - 1)
                    iFirstEntry = iSelectedEntry - osd_size + 1;
            } else {
                iFirstEntry = 0;
                iSelectedEntry = 0;
            }
        }
        else if (mode == SCANF_PREV) {
            if (iSelectedEntry > 0) {
                iSelectedEntry--;
                if (iSelectedEntry < iFirstEntry)
                    iFirstEntry = iSelectedEntry;
            }
        }
        else if (mode == SCANF_NEXT_PAGE) {
            if (iSelectedEntry < iFirstEntry + osd_size - 1) {
                iSelectedEntry = iFirstEntry + osd_size - 1;
                if (iSelectedEntry >= filtered_count)
                    iSelectedEntry = filtered_count - 1;
            } else {
                iSelectedEntry += osd_size;
                iFirstEntry += osd_size;
                if (iSelectedEntry >= filtered_count) {
                    iSelectedEntry = filtered_count - 1;
                    iFirstEntry = iSelectedEntry - osd_size + 1;
                    if (iFirstEntry < 0) iFirstEntry = 0;
                } else if (iFirstEntry + osd_size > filtered_count) {
                    iFirstEntry = filtered_count - osd_size;
                }
            }
        }
        else if (mode == SCANF_PREV_PAGE) {
            if (iSelectedEntry != iFirstEntry) {
                iSelectedEntry = iFirstEntry;
            } else {
                iFirstEntry -= osd_size;
                if (iFirstEntry < 0) iFirstEntry = 0;
                iSelectedEntry = iFirstEntry;
            }
        }
    }
}

void rom_browse_print(void)
{
    int osd_size = OsdGetSize();
    char s[256];

    ScrollReset();

    for (int i = 0; i < osd_size; i++) {
        char leftchar = 0;
        unsigned char stipple = 0;

        if (i < filtered_count) {
            int k = iFirstEntry + i;
            if (k >= filtered_count) {
                OsdWrite(i, "", 0, 0);
                continue;
            }

            rom_entry_t *rom = filtered_roms[k];
            rom_station_t *station = rom_station_get(rom->station_id);

            // Format: "[SYS] ROM Name"
            if (station && browse_station_filter < 0) {
                snprintf(s, sizeof(s), "[%.4s] %s", station->short_name, rom->name);
            } else {
                snprintf(s, sizeof(s), " %s", rom->name);
            }

            // Trim to fit OSD width
            if (strlen(s) > 28) {
                s[27] = 22;  // Continuation character
                s[28] = 0;
            }

            // Arrow indicators for scrolling
            if (!i && k) leftchar = 17;  // Up arrow
            if ((i == osd_size - 1) && (k < filtered_count - 1)) leftchar = 16;  // Down arrow

        } else {
            memset(s, ' ', 29);
            s[29] = 0;
        }

        OsdWriteOffset(i, s, i == (iSelectedEntry - iFirstEntry) && filtered_count,
                       stipple, 0, leftchar);
    }
}

void rom_browse_scroll_name(void)
{
    if (iSelectedEntry >= filtered_count) return;

    rom_entry_t *rom = filtered_roms[iSelectedEntry];
    rom_station_t *station = rom_station_get(rom->station_id);

    static char name[ROM_NAME_LEN + 16];
    if (station && browse_station_filter < 0) {
        snprintf(name, sizeof(name), "[%.4s] %s", station->short_name, rom->name);
    } else {
        snprintf(name, sizeof(name), " %s", rom->name);
    }

    int len = strlen(name);
    int max_len = 30;
    ScrollText(iSelectedEntry - iFirstEntry, name, 0, len, max_len, 1);
}

int rom_browse_select(char *path, char *core_path, char *label)
{
    path[0] = 0;
    core_path[0] = 0;
    label[0] = 0;

    if (!filtered_count || iSelectedEntry >= filtered_count) return 0;

    rom_entry_t *rom = filtered_roms[iSelectedEntry];
    rom_station_t *station = rom_station_get(rom->station_id);

    strcpy(path, rom->path);
    strcpy(label, rom->name);

    if (station && station->core_path[0]) {
        // Find the core RBF file
        char core_search[ROM_PATH_LEN];
        snprintf(core_search, sizeof(core_search), "_%s", station->core_path);

        // The core will be loaded by the menu system
        strcpy(core_path, core_search);
    }

    return 1;
}

int rom_browse_get_selected_index(void)
{
    return iSelectedEntry;
}

int rom_browse_get_first_index(void)
{
    return iFirstEntry;
}

rom_entry_t* rom_get_selected(void)
{
    if (iSelectedEntry >= filtered_count) return NULL;
    return filtered_roms[iSelectedEntry];
}

/*****************************************************************************
 * Sorting
 *****************************************************************************/

static rom_sort_mode_t current_sort_mode = ROM_SORT_NAME_ASC;

static int compare_roms(const void *a, const void *b)
{
    const rom_entry_t *ra = *(const rom_entry_t**)a;
    const rom_entry_t *rb = *(const rom_entry_t**)b;

    switch (current_sort_mode) {
        case ROM_SORT_NAME_ASC:
            return strcasecmp(ra->name, rb->name);
        case ROM_SORT_NAME_DESC:
            return strcasecmp(rb->name, ra->name);
        case ROM_SORT_STATION_ASC:
            if (ra->station_id != rb->station_id)
                return ra->station_id - rb->station_id;
            return strcasecmp(ra->name, rb->name);
        case ROM_SORT_STATION_DESC:
            if (ra->station_id != rb->station_id)
                return rb->station_id - ra->station_id;
            return strcasecmp(ra->name, rb->name);
        case ROM_SORT_DATE_ASC:
            return ra->date - rb->date;
        case ROM_SORT_DATE_DESC:
            return rb->date - ra->date;
        case ROM_SORT_SIZE_ASC:
            return ra->size - rb->size;
        case ROM_SORT_SIZE_DESC:
            return rb->size - ra->size;
        default:
            return 0;
    }
}

void rom_sort(rom_sort_mode_t mode)
{
    current_sort_mode = mode;
    if (filtered_roms && filtered_count > 0) {
        qsort(filtered_roms, filtered_count, sizeof(rom_entry_t*), compare_roms);
    }
}

/*****************************************************************************
 * Search/Filter
 *****************************************************************************/

void rom_filter_set(const char *search_text)
{
    strncpy(search_filter, search_text, sizeof(search_filter) - 1);
    search_filter[sizeof(search_filter) - 1] = 0;
    rebuild_filtered_list();
    iFirstEntry = 0;
    iSelectedEntry = 0;
}

void rom_filter_clear(void)
{
    search_filter[0] = 0;
    rebuild_filtered_list();
}

int rom_filter_active(void)
{
    return search_filter[0] != 0;
}

/*****************************************************************************
 * Utility Functions
 *****************************************************************************/

const char* rom_get_station_name(uint32_t station_id)
{
    rom_station_t *station = rom_station_get(station_id);
    if (station) return station->name;
    return "Unknown";
}

const char* rom_get_display_name(rom_entry_t *rom)
{
    if (rom) return rom->name;
    return "";
}

void rom_format_size(uint32_t size, char *buf, int buf_len)
{
    if (size < 1024) {
        snprintf(buf, buf_len, "%u B", size);
    } else if (size < 1024 * 1024) {
        snprintf(buf, buf_len, "%.1f KB", size / 1024.0);
    } else if (size < 1024 * 1024 * 1024) {
        snprintf(buf, buf_len, "%.1f MB", size / (1024.0 * 1024.0));
    } else {
        snprintf(buf, buf_len, "%.1f GB", size / (1024.0 * 1024.0 * 1024.0));
    }
}

static int match_extension(const char *filename, const char *extensions)
{
    if (!extensions || !extensions[0]) return 1;  // No filter = match all

    const char *dot = strrchr(filename, '.');
    if (!dot) return 0;

    dot++;  // Skip the dot

    // Get the file extension (up to 8 characters to handle longer extensions)
    int ext_len = strlen(dot);
    if (ext_len > 8) ext_len = 8;

    char ext_lower[16];
    for (int i = 0; i < ext_len; i++) {
        ext_lower[i] = tolower(dot[i]);
    }
    ext_lower[ext_len] = 0;

    // Extensions are space-separated (e.g., "cue chd bin iso")
    const char *p = extensions;
    while (*p) {
        // Skip leading spaces
        while (*p == ' ') p++;
        if (!*p) break;

        // Find end of current extension token
        const char *start = p;
        while (*p && *p != ' ') p++;
        int token_len = p - start;

        if (token_len > 0 && token_len <= 8) {
            // Compare case-insensitively
            char cmp[16] = {0};
            for (int i = 0; i < token_len; i++) {
                cmp[i] = tolower(start[i]);
            }

            if (strcmp(ext_lower, cmp) == 0) return 1;
        }
    }

    return 0;
}

static void extract_display_name(const char *filename, char *display_name, int max_len)
{
    // Copy filename without extension
    const char *dot = strrchr(filename, '.');
    int len = dot ? (dot - filename) : strlen(filename);
    if (len >= max_len) len = max_len - 1;

    strncpy(display_name, filename, len);
    display_name[len] = 0;

    // Replace underscores with spaces for nicer display
    for (int i = 0; display_name[i]; i++) {
        if (display_name[i] == '_') display_name[i] = ' ';
    }
}

// ROM counts
int rom_get_count(void)
{
    return g_rom_catalog.rom_count;
}

int rom_get_count_for_station(uint32_t station_id)
{
    rom_station_t *station = rom_station_get(station_id);
    return station ? station->rom_count : 0;
}

rom_entry_t* rom_get_by_index(int index)
{
    if (index < 0 || index >= (int)g_rom_catalog.rom_count) return NULL;
    return &g_rom_catalog.roms[index];
}
