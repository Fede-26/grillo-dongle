cmake_minimum_required(VERSION 3.22)

# Le app vivono in apps/ come normali componenti ESP-IDF.
set(EXTRA_COMPONENT_DIRS apps)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(grillo)
