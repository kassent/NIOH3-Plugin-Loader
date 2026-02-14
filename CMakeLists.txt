# Based on: https://github.com/ttroy50/cmake-examples/blob/master/03-code-generation/protobuf/CMakeLists.txt
cmake_minimum_required(VERSION 3.21)
enable_language(C ASM_MASM)
# Set the project name
project(DINPUT8Wrapper)

set(VCPKG_ROOT_DIR "D:/vcpkg/vcpkg" CACHE PATH "Path to vcpkg root directory")
set(VCPKG_TOOLCHAIN_FILE "${VCPKG_ROOT_DIR}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "Path to vcpkg toolchain file")
set(NIOH3_INSTALL_DIR "E:/SteamLibrary/steamapps/common/Nioh3" CACHE PATH "Path to Nioh3 installation directory")

if(MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    set(VCPKG_TARGET_TRIPLET "x64-windows-static")
    set(VCPKG_LIBRARY_LINKAGE "static")
    set(CMAKE_TOOLCHAIN_FILE "${VCPKG_TOOLCHAIN_FILE}"
        CACHE FILEPATH "Vcpkg toolchain file")
    include("${VCPKG_TOOLCHAIN_FILE}")
endif()

# 设置C++标准
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(ZYDIS_STATIC_BUILD ON)
# 设置所有子项目的输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

if ("${CMAKE_BUILD_TYPE}" MATCHES "Release")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MT")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MT") 

    # Statically compile runtime
    string(REGEX REPLACE "/MD" "/MT" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REGEX REPLACE "/MD" "/MT" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
    string(REGEX REPLACE "/MD" "/MT" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
    string(REGEX REPLACE "/MD" "/MT" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
    
    message(NOTICE "Building in Release mode")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

set(LIB_DINPUT8_WRAPPER DINPUT8)


file(GLOB_RECURSE COMMON_SOURCES 
	"*.asm"
	"*.def"
    "src/*.cpp"
    "src/*.c"
    "include/*.h"
    "include/*.hpp"
)


file(GLOB_RECURSE LIGHTNING_SCANNER_SOURCES 
    "src/LightningScanner/*.cpp"
    "include/LightningScanner/*.h"
    "include/LightningScanner/*.hpp"
)

source_group("LightningScanner" FILES ${LIGHTNING_SCANNER_SOURCES})

add_library(${LIB_DINPUT8_WRAPPER}
  SHARED
  ${COMMON_SOURCES}
  ${LIGHTNING_SCANNER_SOURCES}
)

target_include_directories(${LIB_DINPUT8_WRAPPER}
    PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${LIB_DINPUT8_WRAPPER}
    PUBLIC
        spdlog::spdlog
        fmt::fmt
        dbghelp
)
# 启用 MASM 编译 .asm 文件
set_source_files_properties(dinput8.asm PROPERTIES LANGUAGE ASM_MASM)

message(STATUS "SOURCES_DIR = ${CMAKE_CURRENT_SOURCE_DIR}")

set_target_properties(${LIB_DINPUT8_WRAPPER} PROPERTIES LINK_FLAGS "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/${LIB_DINPUT8_WRAPPER}.def")
set_target_properties(${LIB_DINPUT8_WRAPPER} PROPERTIES LINK_FLAGS "/SUBSYSTEM:WINDOWS")


add_custom_command(TARGET ${LIB_DINPUT8_WRAPPER} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:${LIB_DINPUT8_WRAPPER}>
        "${NIOH3_INSTALL_DIR}/$<TARGET_FILE_NAME:${LIB_DINPUT8_WRAPPER}>"
    COMMENT "Copying ${LIB_DINPUT8_WRAPPER} to ${NIOH3_INSTALL_DIR}/"
)

