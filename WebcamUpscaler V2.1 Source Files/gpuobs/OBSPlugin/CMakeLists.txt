cmake_minimum_required(VERSION 3.28...3.30)

# This folder is intentionally compatible with the official obsproject/obs-plugintemplate.
# Copy src/, data/, and this target definition into an OBS plugin-template checkout,
# or point CMAKE_PREFIX_PATH at a prepared libobs development package.
project(fsr-camera-source VERSION 0.2.0 LANGUAGES CXX)

find_package(libobs REQUIRED)

add_library(fsr-camera-source MODULE)
target_sources(fsr-camera-source PRIVATE
    src/plugin-main.cpp
    ../Shared/SharedFrame.cpp
    ../Shared/SharedFrame.h
)
target_compile_features(fsr-camera-source PRIVATE cxx_std_20)
target_link_libraries(fsr-camera-source PRIVATE OBS::libobs)
target_include_directories(fsr-camera-source PRIVATE ../Shared)

set_target_properties(fsr-camera-source PROPERTIES PREFIX "")

# When built inside the official plugin template, set_target_properties_plugin is provided
# by OBS's CMake helpers and handles Windows bundle/install layout.
if(COMMAND set_target_properties_plugin)
    set_target_properties_plugin(fsr-camera-source PROPERTIES OUTPUT_NAME fsr-camera-source)
endif()
