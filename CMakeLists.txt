cmake_minimum_required(VERSION 3.21)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")

project(amazonscraper)

find_package(FFMPEG REQUIRED)

########## RTSP2RTMP ##########

add_executable(rtsp2rtmp
main.hpp
main.cpp)

target_include_directories(rtsp2rtmp PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(rtsp2rtmp PRIVATE ${FFMPEG_LIBRARY_DIRS})
	
##### INCLUDES #####

include_directories(rtsp2rtmp .)

##### LIBRARIES #####

target_link_libraries(rtsp2rtmp PRIVATE ${FFMPEG_LIBRARIES})
