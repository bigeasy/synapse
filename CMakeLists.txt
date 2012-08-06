cmake_minimum_required (VERSION 2.6)

include(BundleUtilities)
project(synapse)

if (UNIX)
    set(gecko_defs "-DXP_UNIX")
    if(APPLE)
        set (CMAKE_OSX_ARCHITECTURES "i386;x86_64")
        set(gecko_defs "${gecko_defs} -DXP_MACOSX")
    endif()
endif()

set(SOURCES Info.plist
            plugin.c
            say.c
            vendor/attendant/attendant_posix.c
            vendor/attendant/errors.c
            )
add_library(synapse MODULE ${SOURCES})
add_executable(relay say.c vendor/attendant/relay_posix.c vendor/attendant/errors.c)
set(CMAKE_C_FLAGS '-D_SAY')
set_target_properties(synapse PROPERTIES
    BUNDLE 1
    BUNDLE_EXTENSION plugin
    XCODE_ATTRIBUTE_WRAPPER_EXTENSION plugin
    XCODE_ATTRIBUTE_MACH_O_TYPE mh_bundle
    XCODE_ATTRIBUTE_INFOPLIST_FILE ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist
    LINK_FLAGS "-lcurl -fpcc-struct-return -W1,-exported_symbols_list,\"${CMAKE_CURRENT_BINARY_DIR}/exports.txt\"")

if (APPLE)
GET_FILENAME_COMPONENT(NODE /opt/bin/node REALPATH)
INSTALL(TARGETS relay DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/synapse.plugin/Contents/MacOS)
INSTALL(PROGRAMS ${NODE} DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/synapse.plugin/Contents/MacOS)
INSTALL(FILES monitor.js DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/synapse.plugin/Contents/MacOS)
endif()
