#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include "esp_err.h"

#define MAX_FILES 64
#define MAX_FILENAME_LENGTH 32
#define MAX_FILE_SIZE 1024
#define MAX_PATH_LENGTH 256
#define MAX_DIRS 16

typedef struct {
    char name[MAX_FILENAME_LENGTH];
    uint8_t data[MAX_FILE_SIZE];
    uint32_t size;
    bool is_dir;
    int parent_dir;
} File;

esp_err_t fs_init(void);
esp_err_t fs_format_storage(void);
bool fs_create_file(const char* path, const char* content);
bool fs_write_file(const char* path, const uint8_t* data, uint32_t size);
bool fs_read_file(const char* path, uint8_t* data, uint32_t* size);
bool fs_delete_file(const char* path);
void fs_list_files(const char* path);
bool fs_change_dir(const char* path);
void fs_print_working_dir(char* buffer);
bool fs_make_dir(const char* path);
esp_err_t fs_init_storage(void);
esp_err_t fs_write_to_flash(void);
esp_err_t fs_read_from_flash(void);
void fs_periodic_save(void);
void fs_dump_state(void);
esp_err_t fs_format(void);

#endif // FILESYSTEM_H