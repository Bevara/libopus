cmake_minimum_required(VERSION 3.7)
project(Filters)

include(filters.cmake)

add_definitions(-fpic)

SET(LIBOPUS_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/dec_opus.c
)

SET(LIBOPUS_LIB
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/libopus.a
)

SET(LIBOPUS_INC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_filter(libopus
        "${LIBOPUS_SRC}"
        "${LIBOPUS_LIB}"
        []
        ""
        "${LIBOPUS_INC}"
        ""
        "1")
