/*
 * rom_preview.cpp
 * ROM Preview Image System for MiSTer
 *
 * Implementation of preview image loading and online fetching.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <pthread.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>

#include "rom_preview.h"
#include "rom_catalog.h"
#include "file_io.h"
#include "osd.h"
#include "lib/imlib2/Imlib2.h"

// Current preview state
rom_preview_data_t g_current_preview = {};

// Async fetch state
static pthread_t fetch_thread;
static volatile int fetch_in_progress = 0;
static volatile int fetch_cancel = 0;
static char fetch_rom_name[256];
static char fetch_station_name[64];
static char fetch_save_path[1024];

// Batch fetch state
static volatile int batch_cancel = 0;

/*****************************************************************************
 * Initialize/Cleanup
 *****************************************************************************/

int rom_preview_init(void)
{
    memset(&g_current_preview, 0, sizeof(g_current_preview));
    g_current_preview.status = PREVIEW_STATUS_NONE;

    // Ensure preview cache directory exists
    char preview_dir[512];
    snprintf(preview_dir, sizeof(preview_dir), "%s/%s", getFullPath(GAMES_DIR), PREVIEW_CACHE_DIR);
    FileCreatePath(preview_dir);

    return 0;
}

void rom_preview_cleanup(void)
{
    rom_preview_clear();
}

/*****************************************************************************
 * Internet Connectivity Check
 *****************************************************************************/

int rom_preview_check_internet(void)
{
    // Try to resolve a well-known hostname
    struct addrinfo hints = {}, *res = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int result = getaddrinfo("github.com", "443", &hints, &res);
    if (res) freeaddrinfo(res);

    return (result == 0) ? 1 : 0;
}

/*****************************************************************************
 * Local Preview Loading
 *****************************************************************************/

int rom_preview_load_local(rom_entry_t *rom)
{
    if (!rom) return -1;

    rom_preview_clear();

    // Check if ROM has a preview path
    if (rom->has_preview && rom->preview_path[0]) {
        Imlib_Load_Error error = IMLIB_LOAD_ERROR_NONE;
        Imlib_Image img = imlib_load_image_with_error_return(rom->preview_path, &error);

        if (img) {
            imlib_context_set_image(img);
            g_current_preview.image_data = img;
            g_current_preview.width = imlib_image_get_width();
            g_current_preview.height = imlib_image_get_height();
            g_current_preview.status = PREVIEW_STATUS_READY;
            strncpy(g_current_preview.rom_name, rom->name, sizeof(g_current_preview.rom_name) - 1);
            g_current_preview.station_id = rom->station_id;
            return 0;
        }
    }

    // Try to find preview in standard locations
    char preview_path[1024];
    const char *preview_dirs[] = {
        "%s/%s/%s/%s.png",      // games/previews/{station}/{name}.png
        "%s/%s/%s/%s.jpg",
        "%s/%s/%s.png",         // games/{station}/previews/{name}.png with different structure
        NULL
    };

    rom_station_t *station = rom_station_get(rom->station_id);
    const char *station_name = station ? station->short_name : "unknown";

    for (int i = 0; preview_dirs[i]; i++) {
        snprintf(preview_path, sizeof(preview_path), preview_dirs[i],
                 getFullPath(GAMES_DIR), PREVIEW_CACHE_DIR, station_name, rom->name);

        if (FileExists(preview_path, 0)) {
            Imlib_Load_Error error = IMLIB_LOAD_ERROR_NONE;
            Imlib_Image img = imlib_load_image_with_error_return(preview_path, &error);

            if (img) {
                imlib_context_set_image(img);
                g_current_preview.image_data = img;
                g_current_preview.width = imlib_image_get_width();
                g_current_preview.height = imlib_image_get_height();
                g_current_preview.status = PREVIEW_STATUS_READY;
                strncpy(g_current_preview.rom_name, rom->name, sizeof(g_current_preview.rom_name) - 1);
                g_current_preview.station_id = rom->station_id;
                return 0;
            }
        }
    }

    g_current_preview.status = PREVIEW_STATUS_NOT_FOUND;
    return -1;
}

/*****************************************************************************
 * Online Preview Fetching
 *****************************************************************************/

// URL encode a string for API queries
static void url_encode(const char *src, char *dst, size_t dst_size)
{
    static const char *hex = "0123456789ABCDEF";
    size_t i = 0;

    while (*src && i < dst_size - 4) {
        if ((*src >= 'A' && *src <= 'Z') ||
            (*src >= 'a' && *src <= 'z') ||
            (*src >= '0' && *src <= '9') ||
            *src == '-' || *src == '_' || *src == '.' || *src == '~') {
            dst[i++] = *src;
        } else if (*src == ' ') {
            dst[i++] = '+';
        } else {
            dst[i++] = '%';
            dst[i++] = hex[(*src >> 4) & 0x0F];
            dst[i++] = hex[*src & 0x0F];
        }
        src++;
    }
    dst[i] = 0;
}

// Map station short name to libretro-thumbnails system name
static const char* get_libretro_system_name(const char *short_name)
{
    static const struct {
        const char *short_name;
        const char *libretro_name;
    } mappings[] = {
        { "NES",      "Nintendo_-_Nintendo_Entertainment_System" },
        { "SNES",     "Nintendo_-_Super_Nintendo_Entertainment_System" },
        { "Genesis",  "Sega_-_Mega_Drive_-_Genesis" },
        { "SMS",      "Sega_-_Master_System_-_Mark_III" },
        { "GB",       "Nintendo_-_Game_Boy" },
        { "GBC",      "Nintendo_-_Game_Boy_Color" },
        { "GBA",      "Nintendo_-_Game_Boy_Advance" },
        { "N64",      "Nintendo_-_Nintendo_64" },
        { "A2600",    "Atari_-_2600" },
        { "A7800",    "Atari_-_7800" },
        { "A5200",    "Atari_-_5200" },
        { "TG16",     "NEC_-_PC_Engine_-_TurboGrafx_16" },
        { "NeoGeo",   "SNK_-_Neo_Geo" },
        { "Arcade",   "MAME" },
        { "PS1",      "Sony_-_PlayStation" },
        { "PSX",      "Sony_-_PlayStation" },
        { "SegaCD",   "Sega_-_Mega-CD_-_Sega_CD" },
        { "Saturn",   "Sega_-_Saturn" },
        { "S32X",     "Sega_-_32X" },
        { "C64",      "Commodore_-_64" },
        { "Amiga",    "Commodore_-_Amiga" },
        { "AtariST",  "Atari_-_ST" },
        { "MSX",      "Microsoft_-_MSX" },
        { "Spectrum", "Sinclair_-_ZX_Spectrum" },
        { "CPC",      "Amstrad_-_CPC" },
        { "Coleco",   "Coleco_-_ColecoVision" },
        { "Intv",     "Mattel_-_Intellivision" },
        { "Vectrex",  "GCE_-_Vectrex" },
        { "WS",       "Bandai_-_WonderSwan" },
        { "NGP",      "SNK_-_Neo_Geo_Pocket" },
        { NULL, NULL }
    };

    for (int i = 0; mappings[i].short_name; i++) {
        if (strcasecmp(short_name, mappings[i].short_name) == 0) {
            return mappings[i].libretro_name;
        }
    }
    return short_name;
}

// Download a file using wget (available on MiSTer)
static int download_file(const char *url, const char *save_path)
{
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "wget -q -T %d -O \"%s\" \"%s\" 2>/dev/null",
             PREVIEW_FETCH_TIMEOUT, save_path, url);

    int ret = system(cmd);
    if (ret == 0 && FileExists(save_path, 0)) {
        return 0;
    }

    // Clean up failed download
    unlink(save_path);
    return -1;
}

int rom_preview_fetch_online(rom_entry_t *rom)
{
    if (!rom) return -1;

    rom_station_t *station = rom_station_get(rom->station_id);
    if (!station) return -1;

    // Check internet connectivity first
    if (!rom_preview_check_internet()) {
        g_current_preview.status = PREVIEW_STATUS_NO_INTERNET;
        return -1;
    }

    g_current_preview.status = PREVIEW_STATUS_LOADING;

    // Build the save path
    char save_dir[512];
    char save_path[1024];
    snprintf(save_dir, sizeof(save_dir), "%s/%s/%s",
             getFullPath(GAMES_DIR), PREVIEW_CACHE_DIR, station->short_name);
    FileCreatePath(save_dir);

    snprintf(save_path, sizeof(save_path), "%s/%s.png", save_dir, rom->name);

    // Try libretro-thumbnails first
    // URL format: https://thumbnails.libretro.com/{system}/Named_Boxarts/{name}.png
    const char *libretro_system = get_libretro_system_name(station->short_name);
    char encoded_name[512];
    url_encode(rom->name, encoded_name, sizeof(encoded_name));

    char url[1024];

    // Try Named_Boxarts
    snprintf(url, sizeof(url),
             "https://thumbnails.libretro.com/%s/Named_Boxarts/%s.png",
             libretro_system, encoded_name);

    if (download_file(url, save_path) == 0) {
        // Successfully downloaded, now load it
        return rom_preview_load_local(rom);
    }

    // Try Named_Snaps
    snprintf(url, sizeof(url),
             "https://thumbnails.libretro.com/%s/Named_Snaps/%s.png",
             libretro_system, encoded_name);

    if (download_file(url, save_path) == 0) {
        return rom_preview_load_local(rom);
    }

    // Try Named_Titles
    snprintf(url, sizeof(url),
             "https://thumbnails.libretro.com/%s/Named_Titles/%s.png",
             libretro_system, encoded_name);

    if (download_file(url, save_path) == 0) {
        return rom_preview_load_local(rom);
    }

    g_current_preview.status = PREVIEW_STATUS_NOT_FOUND;
    return -1;
}

/*****************************************************************************
 * Async Preview Fetching
 *****************************************************************************/

static void* fetch_thread_func(void *arg)
{
    rom_entry_t *rom = (rom_entry_t*)arg;

    if (!fetch_cancel && rom) {
        rom_preview_fetch_online(rom);
    }

    fetch_in_progress = 0;
    return NULL;
}

int rom_preview_fetch_async(rom_entry_t *rom)
{
    if (fetch_in_progress) {
        // Cancel previous fetch
        fetch_cancel = 1;
        pthread_join(fetch_thread, NULL);
    }

    fetch_cancel = 0;
    fetch_in_progress = 1;
    g_current_preview.status = PREVIEW_STATUS_LOADING;

    if (pthread_create(&fetch_thread, NULL, fetch_thread_func, rom) != 0) {
        fetch_in_progress = 0;
        g_current_preview.status = PREVIEW_STATUS_ERROR;
        return -1;
    }

    return 0;
}

int rom_preview_fetch_poll(void)
{
    return !fetch_in_progress;
}

/*****************************************************************************
 * Display Functions
 *****************************************************************************/

void rom_preview_display(int x, int y, int max_width, int max_height)
{
    if (g_current_preview.status != PREVIEW_STATUS_READY || !g_current_preview.image_data) {
        return;
    }

    Imlib_Image img = (Imlib_Image)g_current_preview.image_data;
    imlib_context_set_image(img);

    // Calculate scaled dimensions maintaining aspect ratio
    int src_w = g_current_preview.width;
    int src_h = g_current_preview.height;

    float scale_w = (float)max_width / src_w;
    float scale_h = (float)max_height / src_h;
    float scale = (scale_w < scale_h) ? scale_w : scale_h;

    int dst_w = (int)(src_w * scale);
    int dst_h = (int)(src_h * scale);

    // Center the image
    int dst_x = x + (max_width - dst_w) / 2;
    int dst_y = y + (max_height - dst_h) / 2;

    // Create scaled image and blend onto framebuffer
    // Note: This would need to integrate with video.cpp's framebuffer system
    // For now, we'll just store the info for the OSD to use
}

void rom_preview_display_text(int x, int y, const char *rom_name, const char *station_name)
{
    // Display text placeholder using OSD
    // This is used when no preview image is available
    char line1[32], line2[32], line3[32];

    snprintf(line1, sizeof(line1), "  [%s]  ", station_name ? station_name : "---");
    snprintf(line2, sizeof(line2), "%.28s", rom_name ? rom_name : "No ROM");

    // Center the text
    int len1 = strlen(line1);
    int len2 = strlen(line2);

    char padded1[32] = {};
    char padded2[32] = {};

    int pad1 = (28 - len1) / 2;
    int pad2 = (28 - len2) / 2;

    for (int i = 0; i < pad1 && i < 28; i++) padded1[i] = ' ';
    strncat(padded1, line1, 28 - pad1);

    for (int i = 0; i < pad2 && i < 28; i++) padded2[i] = ' ';
    strncat(padded2, line2, 28 - pad2);

    // The actual OSD write would happen in the menu code
    // Store for later use
    strncpy(g_current_preview.rom_name, padded2, sizeof(g_current_preview.rom_name) - 1);
}

/*****************************************************************************
 * Cleanup
 *****************************************************************************/

void rom_preview_clear(void)
{
    if (g_current_preview.image_data) {
        Imlib_Image img = (Imlib_Image)g_current_preview.image_data;
        imlib_context_set_image(img);
        imlib_free_image();
    }

    memset(&g_current_preview, 0, sizeof(g_current_preview));
    g_current_preview.status = PREVIEW_STATUS_NONE;
}

preview_status_t rom_preview_get_status(void)
{
    return g_current_preview.status;
}

/*****************************************************************************
 * Cache Management
 *****************************************************************************/

int rom_preview_cache_exists(rom_entry_t *rom)
{
    if (!rom) return 0;

    rom_station_t *station = rom_station_get(rom->station_id);
    if (!station) return 0;

    char path[1024];
    snprintf(path, sizeof(path), "%s/%s/%s/%s.png",
             getFullPath(GAMES_DIR), PREVIEW_CACHE_DIR, station->short_name, rom->name);

    return FileExists(path, 0);
}

int rom_preview_cache_save(rom_entry_t *rom, void *data, int width, int height)
{
    if (!rom || !data) return -1;

    rom_station_t *station = rom_station_get(rom->station_id);
    if (!station) return -1;

    // Create save path
    char save_dir[512];
    char save_path[1024];
    snprintf(save_dir, sizeof(save_dir), "%s/%s/%s",
             getFullPath(GAMES_DIR), PREVIEW_CACHE_DIR, station->short_name);
    FileCreatePath(save_dir);

    snprintf(save_path, sizeof(save_path), "%s/%s.png", save_dir, rom->name);

    // Create image from data and save
    Imlib_Image img = imlib_create_image_using_copied_data(width, height, (DATA32*)data);
    if (!img) return -1;

    imlib_context_set_image(img);
    imlib_image_set_format("png");

    Imlib_Load_Error error = IMLIB_LOAD_ERROR_NONE;
    imlib_save_image_with_error_return(save_path, &error);

    imlib_free_image();

    return (error == IMLIB_LOAD_ERROR_NONE) ? 0 : -1;
}

void rom_preview_cache_clear(void)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s/%s/*",
             getFullPath(GAMES_DIR), PREVIEW_CACHE_DIR);
    system(cmd);
}

void rom_preview_cache_clear_station(uint32_t station_id)
{
    rom_station_t *station = rom_station_get(station_id);
    if (!station) return;

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s/%s/%s/*",
             getFullPath(GAMES_DIR), PREVIEW_CACHE_DIR, station->short_name);
    system(cmd);
}

/*****************************************************************************
 * Batch Download
 *****************************************************************************/

int rom_preview_batch_fetch(uint32_t station_id, preview_progress_cb progress_cb)
{
    batch_cancel = 0;
    int downloaded = 0;

    rom_station_t *station = rom_station_get(station_id);
    if (!station) return 0;

    // Count ROMs for this station
    int total = rom_get_count_for_station(station_id);
    int current = 0;

    // Iterate through all ROMs for this station
    for (uint32_t i = 0; i < (uint32_t)rom_get_count() && !batch_cancel; i++) {
        rom_entry_t *rom = rom_get_by_index(i);
        if (!rom || rom->station_id != station_id) continue;

        current++;

        // Skip if already cached
        if (rom_preview_cache_exists(rom)) {
            if (progress_cb) progress_cb(current, total, rom->name);
            continue;
        }

        // Try to fetch
        if (rom_preview_fetch_online(rom) == 0) {
            downloaded++;
        }

        if (progress_cb) progress_cb(current, total, rom->name);

        // Small delay to avoid hammering the server
        usleep(100000);  // 100ms
    }

    return downloaded;
}

void rom_preview_batch_cancel(void)
{
    batch_cancel = 1;
}
