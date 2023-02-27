SET(CONVERTER_NAME "Converter")

file(GLOB SRC_CPP "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB SRC_H "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")
add_executable(${CONVERTER_NAME} ${SRC_CPP} ${SRC_H})
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/libraries/stb/")

# copy files
add_custom_command(
        TARGET ${CONVERTER_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/textures
                ${CMAKE_CURRENT_BINARY_DIR}/textures)

# edit and continue mode
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /ZI")
  set(CMAKE_SHARED_LINKER_FLAGS "/SAFESEH:NO")
  set(CMAKE_EXE_LINKER_FLAGS "/SAFESEH:NO")
endif()

# build options
set_property(TARGET ${CONVERTER_NAME} PROPERTY CXX_STANDARD 20)
target_compile_definitions(${CONVERTER_NAME} PRIVATE "UNICODE;_UNICODE")