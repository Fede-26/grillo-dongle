cmake_minimum_required(VERSION 3.22)

# Apps live in apps/ as regular ESP-IDF components.
set(EXTRA_COMPONENT_DIRS apps)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(grillo)
