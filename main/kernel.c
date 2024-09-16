#include "include/filesystem.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_sleep.h"
#include "esp_err.h"
#include <inttypes.h>

#define PROMPT "4SkinOS> "
#define MAX_CMD_LENGTH 256

void print_banner(void);

void initialize_console() {
    /* Disable buffering on stdin and stdout */
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    /* Minicom, screen, idf_monitor send CR when ENTER key is pressed */
    uart_vfs_dev_port_set_rx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CR);
    /* Move the caret to the beginning of the next line on '\n' */
    uart_vfs_dev_port_set_tx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CRLF);

    /* Configure UART. Note that REF_TICK is used so that the baud rate remains
     * correct while APB frequency is changing in light sleep mode.
     */
    const uart_config_t uart_config = {
        .baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_REF_TICK,
    };
    /* Install UART driver for interrupt-driven reads and writes */
    ESP_ERROR_CHECK( uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM,
                                         256, 0, 0, NULL, 0) );
    ESP_ERROR_CHECK( uart_param_config(CONFIG_ESP_CONSOLE_UART_NUM, &uart_config) );

    /* Tell VFS to use UART driver */
    uart_vfs_dev_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
}

void shell_task(void *pvParameters) {
    char cmd[MAX_CMD_LENGTH];
    char current_dir[MAX_PATH_LENGTH];
    initialize_console(); 

    vTaskDelay(pdMS_TO_TICKS(100)); // Short delay to ensure output is sent

    // print_banner();

    while (1) {
        memset(cmd, 0, sizeof(cmd));
        int cmd_index = 0;

        fs_print_working_dir(current_dir);
        printf("\n4SkinOS %s> ", current_dir);
        fflush(stdout);

        while (1) {
            char c = getchar();
            if (c == '\n' || c == '\r') {
                printf("\n");
                break;
            } else if (c == 127 || c == '\b') { // Backspace
                if (cmd_index > 0) {
                    cmd_index--;
                    cmd[cmd_index] = '\0';
                    printf("\b \b"); // Move cursor back, print space, move cursor back again
                    fflush(stdout);
                }
            } else if (cmd_index < MAX_CMD_LENGTH - 1) {
                cmd[cmd_index++] = c;
                printf("%c", c); // Echo the character
                fflush(stdout);  // Immediately display the character
            }
        }

        if (strlen(cmd) == 0) {
            continue; // Skip processing for empty commands
        }

        if (strcmp(cmd, "help") == 0) {
            printf("Available commands:\n");
            printf("  help - Show this help message\n");
            printf("  reboot - Reboot the system\n");
            printf("  ls [path] - List files in the current or specified directory\n");
            printf("  cd <path> - Change current directory\n");
            printf("  pwd - Print working directory\n");
            printf("  mkdir <path> - Create a new directory\n");
            printf("  touch <filename> - Create a new file\n");
            printf("  write <filename> <content> - Write content to a file\n");
            printf("  read <filename> - Read content from a file\n");
            printf("  rm <path> - Delete a file or empty directory\n");
            printf("  shutdown - Save filesystem state and shutdown the system\n");
        } else if (strcmp(cmd, "reboot") == 0) {
            printf("Rebooting...\n");
            esp_restart();
        } else if (strncmp(cmd, "ls", 2) == 0) {
            char path[MAX_PATH_LENGTH] = "";
            sscanf(cmd + 2, "%s", path);
            fs_list_files(path[0] ? path : ".");
        } else if (strncmp(cmd, "cd ", 3) == 0) {
            char path[MAX_PATH_LENGTH];
            sscanf(cmd + 3, "%s", path);
            fs_change_dir(path);
        } else if (strcmp(cmd, "pwd") == 0) {
            fs_print_working_dir(current_dir);
            printf("%s\n", current_dir);
        } else if (strncmp(cmd, "mkdir ", 6) == 0) {
            char path[MAX_PATH_LENGTH];
            sscanf(cmd + 6, "%s", path);
            fs_make_dir(path);
        } else if (strncmp(cmd, "touch ", 6) == 0) {
            char filename[MAX_FILENAME_LENGTH];
            sscanf(cmd + 6, "%s", filename);
            fs_create_file(filename, "");
        } else if (strcmp(cmd, "reboot") == 0) {
            printf("Saving filesystem state and rebooting...\n");
            fs_write_to_flash();
            vTaskDelay(pdMS_TO_TICKS(1000)); // Wait for 1 second
            esp_restart();
        } else if (strcmp(cmd, "shutdown") == 0) {
            printf("Saving filesystem state and shutting down...\n");
            esp_err_t err = fs_write_to_flash();
            if (err != ESP_OK) {
                printf("Failed to save filesystem state: %s\n", esp_err_to_name(err));
                printf("Proceeding with shutdown anyway.\n");
            }
            vTaskDelay(100 / portTICK_PERIOD_MS); // Give time for the write operation to complete
            printf("System will reboot now.\n");
            esp_restart(); // Reboot the system
        } else if (strncmp(cmd, "write ", 6) == 0) {
            char filename[MAX_FILENAME_LENGTH];
            char content[MAX_FILE_SIZE];
            sscanf(cmd + 6, "%s %[^\n]", filename, content);
            fs_write_file(filename, (uint8_t*)content, strlen(content));
        } else if (strncmp(cmd, "read ", 5) == 0) {
            char filename[MAX_FILENAME_LENGTH];
            uint8_t content[MAX_FILE_SIZE];
            uint32_t size;
            sscanf(cmd + 5, "%s", filename);
            if (fs_read_file(filename, content, &size)) {
                printf("Content of file %s:\n%.*s\n", filename, (int)size, (char*)content);
            }
        } else if (strncmp(cmd, "rm ", 3) == 0) {
            char path[MAX_PATH_LENGTH];
            sscanf(cmd + 3, "%s", path);
            fs_delete_file(path);
        } else {
            printf("Unknown command: %s\n", cmd);
        }
        fs_periodic_save();
    }
}

void print_banner() {
    printf("\n");
    printf(" /$$   /$$  /$$$$$$  /$$   /$$ /$$$$$$ /$$   /$$       /$$$$$$   /$$$$$$ \n");
    printf("| $$  | $$ /$$__  $$| $$  /$$/|_  $$_/| $$$ | $$      /$$    $$ /$$__  $$\n");
    printf("| $$  | $$| $$  \\__/| $$ /$$/   | $$  | $$$$| $$     | $$    $$| $$  \\__/\n");
    printf("| $$$$$$$$|  $$$$$$ | $$$$$/    | $$  | $$ $$ $$     | $$    $$|  $$$$$$ \n");
    printf("|_____  $$ \\____  $$| $$  $$    | $$  | $$  $$$$     | $$    $$ \\____  $$\n");
    printf("      | $$ /$$  \\ $$| $$\\  $$   | $$  | $$\\  $$$     | $$    $$ /$$  \\ $$\n");
    printf("      | $$|  $$$$$$/| $$ \\  $$ /$$$$$$| $$ \\  $$     |  $$$$$$/|  $$$$$$/\n");
    printf("      |__/ \\______/ |__/  \\__/|______/|__/  \\__/      \\______/  \\______/ \n");
    printf("\n");
    printf("Welcome to 4SKIN OS - Your Bare-Metal Experience\n");
    printf("Version 1.0 - (c) 2024 4SKIN OS - @IMYERF\n");
    printf("\n");
}

void app_main(void) {
    printf("4SkinOS Kernel starting... (Version 3)\n");
    fflush(stdout);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    printf("Initializing filesystem...\n");
    fs_init(); // This now includes reading from flash
    printf("Initializing shell...\n");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(100));
    xTaskCreate(shell_task, "shell", 8192, NULL, 5, NULL);
    printf("Shell initialized.\n");
    print_banner();
    fflush(stdout);

    while(1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        fflush(stdout);
    }
}