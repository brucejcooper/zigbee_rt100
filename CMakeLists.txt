# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

# The esp-mqtt component sits at components/mqtt/esp-mqtt in this IDF checkout
# (no wrapper CMakeLists at components/mqtt), so the default one-level component
# scan misses it. Add its parent dir so it is discovered as component 'esp-mqtt'.
set(EXTRA_COMPONENT_DIRS $ENV{IDF_PATH}/components/mqtt)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(stand_alone_temp_probe)
