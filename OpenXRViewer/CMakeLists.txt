# Add source to this project's executable.
file(GLOB XR_DEMO_HEADERS "xr_demo/*.h")
file(GLOB XR_DEMO_SOURCE "xr_demo/*.cpp")
file(GLOB DESKTOP_HEADERS "desktop/*.h")
file(GLOB DESKTOP_SOURCE "desktop/*.cpp")
add_executable (OpenXRViewer ${XR_DEMO_HEADERS} ${XR_DEMO_SOURCE} ${DESKTOP_HEADERS} ${DESKTOP_SOURCE} "OpenXRViewer.cpp" "OpenXRViewer.h")

# turning this off will show the default openxr demo instead (only useful for testing purposes)
add_definitions(-DUSE_CUSTOM_GRAPHICS_PLUGIN)

# directx
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/libraries/DirectX-Headers/include/directx")

# openxr
add_definitions(-DXR_USE_GRAPHICS_API_D3D12)
add_definitions(-DXR_USE_PLATFORM_WIN32)
set(OPENXR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libraries/OpenXR-SDK)
add_subdirectory(${OPENXR_DIR})
target_include_directories(${PROJECT_NAME} PUBLIC ${OPENXR_DIR}/include)
    
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_link_libraries(${PROJECT_NAME} "${CMAKE_CURRENT_BINARY_DIR}/libraries/OpenXR-SDK/src/loader/openxr_loaderd.lib")
else()
    target_link_libraries(${PROJECT_NAME} "${CMAKE_CURRENT_BINARY_DIR}/libraries/OpenXR-SDK/src/loader/openxr_loader.lib")
endif()

#imgui
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libraries/imgui)

target_sources( ${PROJECT_NAME}
                PRIVATE
                    ${IMGUI_DIR}/imgui_demo.cpp
                    ${IMGUI_DIR}/imgui_draw.cpp
                    ${IMGUI_DIR}/imgui_tables.cpp
                    ${IMGUI_DIR}/imgui_widgets.cpp
                    ${IMGUI_DIR}/imgui.cpp

                PRIVATE
                    ${IMGUI_DIR}/backends/imgui_impl_dx12.cpp
                    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
                )

target_include_directories( ${PROJECT_NAME}
                            PUBLIC ${IMGUI_DIR}
                            PUBLIC ${IMGUI_DIR}/backends )

# assets
file(COPY "fonts/Montserrat-Regular.ttf" DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY "fonts/remixicon.ttf" DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/shaders
                ${CMAKE_CURRENT_BINARY_DIR}/shaders)

add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/models
                ${CMAKE_CURRENT_BINARY_DIR}/models)
                
add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
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
set_property(TARGET OpenXRViewer PROPERTY CXX_STANDARD 20)
target_compile_definitions(${PROJECT_NAME} PRIVATE "UNICODE;_UNICODE")
target_link_libraries(${PROJECT_NAME} "d3d12.lib" "dxgi.lib" "dxguid.lib" "d3dcompiler.lib")
target_compile_options(${PROJECT_NAME} PRIVATE /W3 /w34456 /w34189 /w44305 /w44244 /w44267)