# Copyright (c) 2020 大前良介 (OHMAE Ryosuke)
#
# This software is released under the MIT License.
# http://opensource.org/licenses/MIT

cmake_minimum_required(VERSION 3.16)
project(png2aa C)
set(CMAKE_C_STANDARD 99)

include(FindFreetype)
include_directories(${FREETYPE_INCLUDE_DIRS})

include(FindPNG)
include_directories(${PNG_INCLUDE_DIRS})
link_directories(${PNG_LIBRARIES})

find_package(Threads REQUIRED)

add_executable(make_code_book make_code_book.c common.c)
add_executable(png2txt png2txt.c common.c)
add_executable(txt2png txt2png.c common.c)
add_executable(scalar_png2txt scalar_png2txt.c common.c)

target_link_libraries(make_code_book ${FREETYPE_LIBRARIES})

target_link_libraries(png2txt ${PNG_LIBRARIES})
target_link_libraries(png2txt Threads::Threads)

target_link_libraries(txt2png ${FREETYPE_LIBRARIES})
target_link_libraries(txt2png ${PNG_LIBRARIES})

target_link_libraries(scalar_png2txt ${FREETYPE_LIBRARIES})
target_link_libraries(scalar_png2txt ${PNG_LIBRARIES})
