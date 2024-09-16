#include "include/filesystem.h"
#include <string.h>
#include <stdio.h>
#include "esp_system.h"
#include <inttypes.h>
#include "esp_partition.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <inttypes.h>

#define STORAGE_NAMESPACE "storage"
#define SECTOR_SIZE 4096
#define NUM_SECTORS 32
#define HEADER_SIZE 12
#define MIN(a, b) ((a) < (b) ? (a) : (b))

// Magic (4 bytes) + Timestamp (4 bytes) + num_files (4 bytes)


static const char *TAG = "filesystem";
static const esp_partition_t* storage_partition;

static File files[MAX_FILES];
static uint32_t num_files = 0;
static int current_dir = 0;
static char current_path[MAX_PATH_LENGTH] = "/";
static uint32_t current_sector = 0;
static const uint8_t HEADER_MAGIC[4] = {'F', 'S', 'Y', 'S'};


esp_err_t fs_init(void) {
    ESP_LOGI(TAG, "Initializing filesystem...");

    esp_err_t err = fs_init_storage();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize storage: %s", esp_err_to_name(err));
        return err;
    }

    err = fs_read_from_flash();
    if (err == ESP_ERR_NOT_FOUND) {
        ESP_LOGI(TAG, "Filesystem not found. Formatting...");
        err = fs_format();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to format storage: %s", esp_err_to_name(err));
            return err;
        }
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read filesystem state from flash: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Filesystem initialization complete. Root directory: /, Number of files: %" PRIu32, num_files);
    return ESP_OK;
}

void fs_dump_state(void) {
    ESP_LOGI(TAG, "Current filesystem state:");
    ESP_LOGI(TAG, "Number of files: %" PRIu32, num_files);
    for (int i = 0; i < num_files; i++) {
        ESP_LOGI(TAG, "File %d: %s, is_dir: %d, parent_dir: %d, size: %" PRIu32,
            i, files[i].name, files[i].is_dir, files[i].parent_dir, files[i].size);
    }
}

static int find_file(const char* path) {
    char temp_path[MAX_PATH_LENGTH];
    strcpy(temp_path, path);
    char* token = strtok(temp_path, "/");

    // Start from root if path is absolute
    int current = (path[0] == '/') ? 0 : current_dir;

    while (token != NULL) {
        bool found = false;
        for (int i = 0; i < num_files; i++) {
            if (files[i].parent_dir == current && strcmp(files[i].name, token) == 0) {
                current = i;
                found = true;
                break;
            }
        }
        if (!found) return -1;
        token = strtok(NULL, "/");
    }
    return current;
}


bool fs_create_file(const char* path, const char* content) {
    // Find the parent directory
    char parent_path[MAX_PATH_LENGTH];
    strncpy(parent_path, path, MAX_PATH_LENGTH - 1);
    parent_path[MAX_PATH_LENGTH - 1] = '\0';

    char* last_slash = strrchr(parent_path, '/');
    char file_name[MAX_FILENAME_LENGTH];

    if (last_slash) {
        strcpy(file_name, last_slash + 1);
        if (last_slash == parent_path) {
            // The parent is root "/"
            strcpy(parent_path, "/");
        } else {
            *last_slash = '\0';  // Terminate parent_path at the last slash
        }
    } else {
        // No slash found, current directory is the parent
        strcpy(file_name, parent_path);
        strcpy(parent_path, ".");
    }

    int parent_dir = find_file(parent_path);
    if (parent_dir == -1 || !files[parent_dir].is_dir) {
        ESP_LOGE(TAG, "Parent directory not found: %s", parent_path);
        return false;
    }

    // Check if file already exists in parent directory
    for (int i = 0; i < num_files; i++) {
        if (files[i].parent_dir == parent_dir && strcmp(files[i].name, file_name) == 0) {
            ESP_LOGE(TAG, "File already exists: %s", path);
            return false;
        }
    }

    // Create the new file
    if (num_files >= MAX_FILES) {
        ESP_LOGE(TAG, "Maximum number of files reached");
        return false;
    }

    strncpy(files[num_files].name, file_name, MAX_FILENAME_LENGTH - 1);
    files[num_files].name[MAX_FILENAME_LENGTH - 1] = '\0';
    files[num_files].is_dir = false;
    files[num_files].parent_dir = parent_dir;
    files[num_files].size = strlen(content);

    // Copy content to data field
    if (files[num_files].size > MAX_FILE_SIZE) {
        files[num_files].size = MAX_FILE_SIZE;
        ESP_LOGW(TAG, "File content truncated to %d bytes", MAX_FILE_SIZE);
    }
    memcpy(files[num_files].data, content, files[num_files].size);

    num_files++;
    ESP_LOGI(TAG, "File created: %s in directory %s", file_name, parent_path);
    return true;
}


bool fs_write_file(const char* filename, const uint8_t* content, uint32_t size) {
    char full_path[MAX_PATH_LENGTH];
    
    if (filename[0] != '/') {
        size_t current_path_len = strlen(current_path);
        size_t filename_len = strlen(filename);
        size_t separator_len = (current_path[current_path_len - 1] == '/') ? 0 : 1;
        
        if (current_path_len + separator_len + filename_len >= MAX_PATH_LENGTH) {
            printf("Path too long\n");
            return false;
        }
        
        strcpy(full_path, current_path);
        if (separator_len) {
            strcat(full_path, "/");
        }
        strcat(full_path, filename);
    } else {
        if (strlen(filename) >= MAX_PATH_LENGTH) {
            printf("Path too long\n");
            return false;
        }
        strcpy(full_path, filename);
    }

    int file_index = find_file(full_path);
    if (file_index == -1) {
        // File doesn't exist, create it
        if (!fs_create_file(full_path, (const char*)content)) {
            printf("Failed to create file: %s\n", full_path);
            return false;
        }
        file_index = find_file(full_path);
    }

    if (file_index == -1 || files[file_index].is_dir) {
        printf("Invalid file: %s\n", full_path);
        return false;
    }

    if (size > MAX_FILE_SIZE) {
        printf("Content too large for file: %s\n", full_path);
        return false;
    }

    memcpy(files[file_index].data, content, size);
    files[file_index].size = size;
    printf("Content written to file: %s (%" PRIu32 " bytes)\n", full_path, size);
    return true;
}

bool fs_read_file(const char* path, uint8_t* data, uint32_t* size) {
    int file_index = find_file(path);
    if (file_index == -1 || files[file_index].is_dir) return false;

    memcpy(data, files[file_index].data, files[file_index].size);
    *size = files[file_index].size;
    return true;
}

bool fs_delete_file(const char* path) {
    int file_index = find_file(path);
    if (file_index == -1) {
        printf("File or directory not found: %s\n", path);
        return false;
    }

    if (files[file_index].is_dir) {
        // Check if directory is empty
        for (int i = 0; i < num_files; i++) {
            if (files[i].parent_dir == file_index) {
                printf("Cannot delete non-empty directory: %s\n", path);
                return false;
            }
        }
    }

    // Shift all files after the deleted one
    for (int i = file_index; i < num_files - 1; i++) {
        files[i] = files[i + 1];
    }
    num_files--;
    fs_periodic_save();
    return true;
}

void fs_list_files(const char* path) {
    int dir_index = (strcmp(path, ".") == 0 || strlen(path) == 0) ? current_dir : find_file(path);

    if (dir_index == -1 || !files[dir_index].is_dir) {
        printf("Invalid directory: %s\n", path);
        return;
    }

    bool empty = true;
    for (int i = 0; i < num_files; i++) {
        if (files[i].parent_dir == dir_index) {
            empty = false;
            printf("%s%s", files[i].name, files[i].is_dir ? "/" : "");
            if (!files[i].is_dir) {
                printf(" (%" PRIu32 " bytes)", files[i].size);
            }
        }
    }
    if (empty) {
        printf("(empty)");
    }
}

bool fs_change_dir(const char* path) {
    if (strcmp(path, "/") == 0) {
        current_dir = 0;
        strcpy(current_path, "/");
        return true;
    }

    if (strcmp(path, "..") == 0) {
        if (current_dir != 0) {
            current_dir = files[current_dir].parent_dir;
            char* last_slash = strrchr(current_path, '/');
            if (last_slash != current_path) {
                *last_slash = '\0';
            } else {
                *(last_slash + 1) = '\0';
            }
        }
        return true;
    }

    int dir_index = find_file(path);
    if (dir_index == -1 || !files[dir_index].is_dir) {
        printf("Invalid directory: %s\n", path);
        return false;
    }

    current_dir = dir_index;

    // Update current_path.
    if (path[0] == '/') {
        strncpy(current_path, path, MAX_PATH_LENGTH - 1);
    } else {
        if (strcmp(current_path, "/") != 0) {
            strncat(current_path, "/", MAX_PATH_LENGTH - strlen(current_path) - 1);
        }
        strncat(current_path, path, MAX_PATH_LENGTH - strlen(current_path) - 1);
    }

    return true;
}

void fs_print_working_dir(char* buffer) {
    strcpy(buffer, current_path);
}

bool fs_make_dir(const char* path) {
    if (num_files >= MAX_FILES) {
        printf("Maximum number of files reached.\n");
        return false;
    }

    char full_path[MAX_PATH_LENGTH];
    if (path[0] == '/') {
        strncpy(full_path, path, MAX_PATH_LENGTH - 1);
        full_path[MAX_PATH_LENGTH - 1] = '\0';
    } else {
        size_t current_path_len = strlen(current_path);
        size_t path_len = strlen(path);
        if (current_path_len + path_len + 2 > MAX_PATH_LENGTH) {
            printf("Path too long\n");
            return false;
        }
        strcpy(full_path, current_path);
        if (full_path[current_path_len - 1] != '/') {
            strcat(full_path, "/");
        }
        strcat(full_path, path);
    }

    char parent_path[MAX_PATH_LENGTH];
    strncpy(parent_path, full_path, MAX_PATH_LENGTH - 1);
    parent_path[MAX_PATH_LENGTH - 1] = '\0';

    char* last_slash = strrchr(parent_path, '/');
    char dir_name[MAX_FILENAME_LENGTH];

    if (last_slash) {
        strcpy(dir_name, last_slash + 1);
        if (last_slash == parent_path) {
            // The parent is root "/"
            strcpy(parent_path, "/");
        } else {
            *last_slash = '\0';  // Terminate parent_path at the last slash
        }
    } else {
        // No slash found, current directory is the parent
        strcpy(dir_name, parent_path);
        strcpy(parent_path, ".");
    }

    int parent_dir = find_file(parent_path);
    if (parent_dir == -1 || !files[parent_dir].is_dir) {
        printf("Parent directory not found: %s\n", parent_path);
        return false;
    }

    // Check if directory already exists in parent directory
    for (int i = 0; i < num_files; i++) {
        if (files[i].parent_dir == parent_dir && strcmp(files[i].name, dir_name) == 0 && files[i].is_dir) {
            printf("Directory already exists: %s\n", path);
            return false;
        }
    }

    strncpy(files[num_files].name, dir_name, MAX_FILENAME_LENGTH - 1);
    files[num_files].name[MAX_FILENAME_LENGTH - 1] = '\0';
    files[num_files].is_dir = true;
    files[num_files].parent_dir = parent_dir;
    files[num_files].size = 0;

    num_files++;
    ESP_LOGI(TAG, "Directory created: %s in directory %s", dir_name, parent_path);
    fs_periodic_save();
    return true;
}


esp_err_t fs_init_storage(void) {
    storage_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "storage");
    if (storage_partition == NULL) {
        ESP_LOGE(TAG, "Failed to find storage partition");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Storage partition found: offset 0x%" PRIx32 ", size 0x%" PRIx32, 
             storage_partition->address, storage_partition->size);

    // Check if the filesystem is initialized
    uint8_t header[HEADER_SIZE];
    esp_err_t err = esp_partition_read(storage_partition, 0, header, HEADER_SIZE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read storage partition: %s", esp_err_to_name(err));
        return err;
    }

    if (memcmp(header, HEADER_MAGIC, sizeof(HEADER_MAGIC)) != 0) {
        ESP_LOGI(TAG, "Filesystem not initialized. Formatting...");
        return fs_format_storage();
    }

    ESP_LOGI(TAG, "Filesystem already initialized");
    return ESP_OK;
}

esp_err_t fs_format_storage(void) {
    ESP_LOGI(TAG, "Formatting storage partition");

    // Erase the entire partition
    esp_err_t err = esp_partition_erase_range(storage_partition, 0, storage_partition->size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase storage partition: %s", esp_err_to_name(err));
        return err;
    }

    // Initialize with an empty root directory
    num_files = 1;
    files[0] = (File){
        .name = "/",
        .is_dir = true,
        .parent_dir = -1,
        .size = 0
    };

    // Write the initial filesystem state
    return fs_write_to_flash();
}

esp_err_t fs_format(void) {
    ESP_LOGI(TAG, "Formatting filesystem");
    num_files = 1;
    memset(files, 0, sizeof(files));
    strcpy(files[0].name, "/");
    files[0].is_dir = true;
    files[0].parent_dir = -1;
    current_sector = 0;
    return fs_write_to_flash();
}

esp_err_t fs_write_to_flash(void) {
    uint32_t timestamp = esp_log_timestamp();
    ESP_LOGI(TAG, "Writing filesystem state to flash, starting from sector %" PRIu32, current_sector);

    size_t total_size = HEADER_SIZE + sizeof(File) * num_files;
    uint32_t sectors_needed = (total_size + SECTOR_SIZE - 1) / SECTOR_SIZE;

    ESP_LOGI(TAG, "Total size: %zu, Sectors needed: %" PRIu32, total_size, sectors_needed);

    if (sectors_needed > NUM_SECTORS) {
        ESP_LOGE(TAG, "Data size exceeds total available storage");
        return ESP_ERR_INVALID_SIZE;
    }

    for (uint32_t i = 0; i < sectors_needed; i++) {
        uint32_t sector = (current_sector + i) % NUM_SECTORS;
        esp_err_t err = esp_partition_erase_range(storage_partition, sector * SECTOR_SIZE, SECTOR_SIZE);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to erase sector %" PRIu32 ": %s", sector, esp_err_to_name(err));
            return err;
        }

        size_t write_size = (i == 0) ? SECTOR_SIZE : MIN(SECTOR_SIZE, total_size - (i * SECTOR_SIZE));
        uint8_t* write_buffer = calloc(1, write_size); // Use calloc to zero-initialize
        if (!write_buffer) {
            ESP_LOGE(TAG, "Failed to allocate write buffer");
            return ESP_ERR_NO_MEM;
        }

        if (i == 0) {
            // Write header in the first sector
            memcpy(write_buffer, HEADER_MAGIC, sizeof(HEADER_MAGIC));
            memcpy(write_buffer + 4, &timestamp, sizeof(timestamp));
            memcpy(write_buffer + 8, &num_files, sizeof(num_files));
            memcpy(write_buffer + HEADER_SIZE, files, MIN(write_size - HEADER_SIZE, sizeof(File) * num_files));
        } else {
            // Write remaining file data in subsequent sectors
            size_t offset = (i * SECTOR_SIZE) - HEADER_SIZE;
            memcpy(write_buffer, (uint8_t*)files + offset, write_size);
        }

        err = esp_partition_write(storage_partition, sector * SECTOR_SIZE, write_buffer, write_size);
        free(write_buffer);

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write sector %" PRIu32 ": %s", sector, esp_err_to_name(err));
            return err;
        } else {
            ESP_LOGI(TAG, "Successfully wrote sector %" PRIu32, sector);
        }
    }

    current_sector = (current_sector + sectors_needed) % NUM_SECTORS;
    ESP_LOGI(TAG, "Filesystem state written to flash, next write will start at sector %" PRIu32, current_sector);
    return ESP_OK;
}


esp_err_t fs_read_from_flash(void) {
    uint32_t latest_timestamp = 0;
    uint32_t latest_sector = 0;

    ESP_LOGI(TAG, "Attempting to read filesystem state from flash");

    // Find the latest valid filesystem state
    for (uint32_t i = 0; i < NUM_SECTORS; i++) {
        uint8_t header[HEADER_SIZE];
        esp_err_t err = esp_partition_read(storage_partition, i * SECTOR_SIZE, header, HEADER_SIZE);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read sector %" PRIu32 " header: %s", i, esp_err_to_name(err));
            continue;
        }

        uint32_t magic;
        memcpy(&magic, header, sizeof(magic));
        if (memcmp(&magic, HEADER_MAGIC, sizeof(HEADER_MAGIC)) != 0) {
            continue;  // Skip invalid sectors
        }

        uint32_t timestamp;
        memcpy(&timestamp, header + 4, sizeof(timestamp));

        if (timestamp > latest_timestamp) {
            latest_timestamp = timestamp;
            latest_sector = i;
        }
    }

    if (latest_timestamp == 0) {
        ESP_LOGI(TAG, "No valid filesystem data found in flash");
        // Initialize filesystem with root directory
        num_files = 1;
        memset(files, 0, sizeof(files));
        strcpy(files[0].name, "/");
        files[0].is_dir = true;
        files[0].parent_dir = -1;
        current_sector = 0;
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Latest filesystem state found in sector %" PRIu32 " with timestamp %" PRIu32, latest_sector, latest_timestamp);

    // Read the entire filesystem state from the latest sector
    uint8_t* read_buffer = malloc(SECTOR_SIZE);
    if (!read_buffer) {
        ESP_LOGE(TAG, "Failed to allocate read buffer");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_partition_read(storage_partition, latest_sector * SECTOR_SIZE, read_buffer, SECTOR_SIZE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read latest sector: %s", esp_err_to_name(err));
        free(read_buffer);
        return err;
    }

    // Parse the header
    memcpy(&num_files, read_buffer + 8, sizeof(num_files));
    ESP_LOGI(TAG, "Number of files in filesystem: %" PRIu32, num_files);

    // Validate num_files
    if (num_files == 0 || num_files > MAX_FILES) {
        ESP_LOGE(TAG, "Invalid number of files: %" PRIu32, num_files);
        free(read_buffer);
        return ESP_ERR_INVALID_SIZE;
    }

    // Copy file data
    size_t files_data_size = sizeof(File) * num_files;
    if (files_data_size > SECTOR_SIZE - HEADER_SIZE) {
        ESP_LOGE(TAG, "File data size exceeds sector size");
        free(read_buffer);
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(files, read_buffer + HEADER_SIZE, files_data_size);

    free(read_buffer);

    // Update current_sector for the next write operation
    current_sector = (latest_sector + 1) % NUM_SECTORS;

    ESP_LOGI(TAG, "Filesystem state restored from flash");
    ESP_LOGI(TAG, "Next write will start at sector %" PRIu32, current_sector);

    // Log files for verification
    for (uint32_t i = 0; i < num_files; i++) {
        ESP_LOGI(TAG, "File %" PRIu32 ": %s, is_dir: %d, parent_dir: %d, size: %" PRIu32,
                 i, files[i].name, files[i].is_dir, files[i].parent_dir, files[i].size);
    }

    return ESP_OK;
}



// Add this function to periodically save the filesystem state
void fs_periodic_save(void) {
    static uint32_t last_save_time = 0;
    uint32_t current_time = esp_log_timestamp();

    if (current_time - last_save_time > 300000) { // Save every 5 minutes (300,000 ms)
        fs_write_to_flash();
        last_save_time = current_time;
    }
}






