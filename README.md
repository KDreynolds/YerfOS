# 4SkinOS

4SkinOS is a custom operating system for ESP32 microcontrollers, built on top of FreeRTOS and the ESP-IDF framework. It provides a shell interface, basic filesystem operations, and a platform for embedded application development.

## Features

- Custom operating system for ESP32
- Interactive shell interface
- In-memory filesystem with basic operations
- Task management and scheduling (leveraging FreeRTOS)
- Hardware abstraction layer (utilizing ESP-IDF)

## Architecture

4SkinOS is structured as follows:

1. Hardware: ESP32 microcontroller
2. Low-level drivers: ESP-IDF framework
3. Real-time kernel: FreeRTOS
4. Operating System: 4SkinOS
   - Shell interface
   - Filesystem management
   - Task and resource management
   - System calls and APIs

While 4SkinOS uses FreeRTOS and ESP-IDF, it extends their functionality to create a complete, custom operating system environment.

## Getting Started

### Prerequisites

- ESP-IDF (Espressif IoT Development Framework)
- ESP32 development board

### Building and Flashing

1. Clone the repository:
   ```
   git clone https://github.com/yourusername/4SkinOS.git
   cd 4SkinOS
   ```

2. Set up the ESP-IDF environment:
   ```
   . $HOME/esp/esp-idf/export.sh
   ```

3. Configure the project:
   ```
   idf.py menuconfig
   ```

4. Build the project:
   ```
   idf.py build
   ```

5. Flash the project to your ESP32:
   ```
   idf.py -p /dev/ttyUSB0 flash monitor
   ```
   Replace `/dev/ttyUSB0` with the appropriate port for your system.

## Usage

Once flashed, the system will boot into a shell interface. Available commands include:

- `help`: Show available commands
- `reboot`: Reboot the system
- `ls [path]`: List files in the current or specified directory
- `cd <path>`: Change current directory
- `pwd`: Print working directory
- `mkdir <path>`: Create a new directory
- `touch <filename>`: Create a new file
- `write <filename> <content>`: Write content to a file
- `read <filename>`: Read content from a file
- `rm <path>`: Delete a file or empty directory

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- Espressif for the ESP-IDF framework
- FreeRTOS for the real-time operating system
