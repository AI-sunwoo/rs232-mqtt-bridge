# RS232 to MQTT Bridge - ESP32-S3
# Top-level CMakeLists.txt

cmake_minimum_required(VERSION 3.16)

# Project version (update this for OTA)
set(PROJECT_VER "1.0.0")

# Include ESP-IDF build system
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Project name
project(rs232_mqtt_bridge VERSION ${PROJECT_VER})
