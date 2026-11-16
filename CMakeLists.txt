cmake_minimum_required(VERSION 3.16)
project(rk_camera_isp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT MSVC)
    add_compile_options(-Wall -Wextra -Wpedantic)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
endif()

set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
option(BUILD_HOST_TESTS "Build dependency-free lifecycle and CLI tests" ON)
option(BUILD_BSP_TOOLS "Build read-only media graph and rkAIQ status tools" ON)
option(BUILD_RK_DEMO "Build the Rockchip MIPI camera application" ${UNIX})
option(REQUIRE_RK_DEMO "Fail configure when RK demo dependencies are missing" OFF)
option(BUILD_SOFTWARE_DEMO "Build the dependency-light VirtualSensor + Software ISP demo" ON)
option(ENABLE_ASAN_UBSAN "Enable AddressSanitizer and UndefinedBehaviorSanitizer for host targets" OFF)

if(ENABLE_ASAN_UBSAN AND NOT MSVC)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

add_library(software_isp STATIC
    lib/isp/isp_pipeline.cpp
    lib/isp/ae_controller.cpp
    lib/isp/isp_profile.cpp)
target_include_directories(software_isp PUBLIC ${PROJECT_ROOT}/include)

add_library(virtual_camera STATIC
    lib/camera/raw10.cpp
    lib/camera/virtual_sensor.cpp
    lib/camera/virtual_sensor_config.cpp)
target_include_directories(virtual_camera PUBLIC ${PROJECT_ROOT}/include)

add_library(demo_ai STATIC lib/ai/inference_backend.cpp)
target_include_directories(demo_ai PUBLIC ${PROJECT_ROOT}/include)
target_link_libraries(demo_ai PUBLIC software_isp)

if(BUILD_SOFTWARE_DEMO)
    add_executable(camera_isp_demo
        tools/camera_isp_demo.cpp
        lib/frame_pairer.cpp
        lib/composite_processor.cpp)
    target_include_directories(camera_isp_demo PRIVATE ${PROJECT_ROOT}/include)
    target_link_libraries(camera_isp_demo PRIVATE virtual_camera software_isp demo_ai)
endif()

if(BUILD_HOST_TESTS)
    enable_testing()
    add_executable(isp_host_tests
        tests/camera_lifecycle_test.cpp
        lib/app_options.cpp
        lib/composite_processor.cpp)
    target_include_directories(isp_host_tests PRIVATE ${PROJECT_ROOT}/include)
    add_test(NAME isp_host_tests COMMAND isp_host_tests)
    add_executable(composite_processor_test
        tests/composite_processor_test.cpp
        lib/composite_processor.cpp)
    target_include_directories(composite_processor_test PRIVATE ${PROJECT_ROOT}/include)
    add_test(NAME composite_processor_test COMMAND composite_processor_test)
    add_executable(encoded_packet_fanout_test
        tests/encoded_packet_fanout_test.cpp
        lib/encoded_packet_fanout.cpp)
    target_include_directories(encoded_packet_fanout_test PRIVATE ${PROJECT_ROOT}/include)
    add_test(NAME encoded_packet_fanout_test COMMAND encoded_packet_fanout_test)
    add_executable(frame_pairer_test
        tests/frame_pairer_test.cpp
        lib/frame_pairer.cpp)
    target_include_directories(frame_pairer_test PRIVATE ${PROJECT_ROOT}/include)
    add_test(NAME frame_pairer_test COMMAND frame_pairer_test)
    add_executable(media_graph_validator_test
        tests/media_graph_validator_test.cpp
        lib/media_graph_validator.cpp)
    target_include_directories(media_graph_validator_test PRIVATE ${PROJECT_ROOT}/include)
    add_test(NAME media_graph_validator_test COMMAND media_graph_validator_test)
    add_executable(software_isp_test
        tests/software_isp_test.cpp
        )
    target_include_directories(software_isp_test PRIVATE ${PROJECT_ROOT}/include)
    target_link_libraries(software_isp_test PRIVATE software_isp)
    add_test(NAME software_isp_test COMMAND software_isp_test)
    add_executable(raw_virtual_sensor_test
        tests/raw_virtual_sensor_test.cpp)
    target_include_directories(raw_virtual_sensor_test PRIVATE ${PROJECT_ROOT}/include)
    target_link_libraries(raw_virtual_sensor_test PRIVATE virtual_camera)
    add_test(NAME raw_virtual_sensor_test COMMAND raw_virtual_sensor_test)
    add_executable(ae_profile_test tests/ae_profile_test.cpp)
    target_include_directories(ae_profile_test PRIVATE ${PROJECT_ROOT}/include)
    target_link_libraries(ae_profile_test PRIVATE software_isp)
    add_test(NAME ae_profile_test COMMAND ae_profile_test)
    add_executable(ai_backend_test tests/ai_backend_test.cpp)
    target_include_directories(ai_backend_test PRIVATE ${PROJECT_ROOT}/include)
    target_link_libraries(ai_backend_test PRIVATE demo_ai software_isp)
    add_test(NAME ai_backend_test COMMAND ai_backend_test)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME project_structure_validation
                 COMMAND ${Python3_EXECUTABLE} ${PROJECT_ROOT}/tests/validate_project.py)
        add_test(NAME isp_quality_analyzer_test
                 COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${PROJECT_ROOT}/tests -p "test_isp_quality_analyzer.py")
    endif()
endif()

if(BUILD_BSP_TOOLS)
    add_executable(media_graph_validator
        tools/media_graph_validator_main.cpp
        lib/media_graph_validator.cpp)
    target_include_directories(media_graph_validator PRIVATE ${PROJECT_ROOT}/include)

    add_executable(isp_control_cli
        tools/isp_control_cli.cpp
        lib/aiq_controller.cpp)
    target_include_directories(isp_control_cli PRIVATE ${PROJECT_ROOT}/include)

    add_executable(isp_debug_tool
        tools/isp_debug_tool.cpp
        lib/aiq_controller.cpp)
    target_include_directories(isp_debug_tool PRIVATE ${PROJECT_ROOT}/include)
endif()

if(BUILD_RK_DEMO AND UNIX)
    # These are the only vendor dependencies of the single MIPI path. The old
    # FFmpeg decoder/RTMP stack is intentionally absent from this target.
    set(RKNN_API_PATH ${PROJECT_ROOT}/../rknn CACHE PATH "RKNN SDK root")
    set(MPP_PATH      ${PROJECT_ROOT}/../mpp CACHE PATH "MPP SDK root")
    set(RGA_PATH      ${PROJECT_ROOT}/../rga CACHE PATH "RGA SDK root")
    set(OPENCV_ROOT   ${PROJECT_ROOT}/../opencv4-1 CACHE PATH "OpenCV SDK root")

    set(RK_DEMO_DEPS_OK TRUE)
    foreach(required_path IN ITEMS
        ${RKNN_API_PATH}/include
        ${RKNN_API_PATH}/lib/librknn_api.so
        ${MPP_PATH}/include
        ${MPP_PATH}/lib/librockchip_mpp.so
        ${RGA_PATH}/include
        ${RGA_PATH}/lib/librga.so
        ${OPENCV_ROOT}/include
        ${OPENCV_ROOT}/include/opencv4
        ${OPENCV_ROOT}/lib)
        if(NOT EXISTS ${required_path})
            message(STATUS "Missing RK demo dependency: ${required_path}")
            set(RK_DEMO_DEPS_OK FALSE)
        endif()
    endforeach()

    file(GLOB OpenCV_LIBS_FILES "${OPENCV_ROOT}/lib/libopencv_*.so")
    if(NOT OpenCV_LIBS_FILES)
        message(STATUS "No OpenCV shared libraries found under ${OPENCV_ROOT}/lib")
        set(RK_DEMO_DEPS_OK FALSE)
    endif()

    if(NOT RK_DEMO_DEPS_OK)
        if(REQUIRE_RK_DEMO)
            message(FATAL_ERROR "RK demo dependencies are incomplete; use -DREQUIRE_RK_DEMO=OFF for host-only tests")
        endif()
        message(WARNING "RK demo target skipped because vendor/media dependencies are incomplete")
    else()
        add_executable(myDemo
            main.cpp
            lib/app_options.cpp
            lib/dual_camera_pipeline.cpp
            lib/mpp_encoder.cpp
            lib/postprocess.cpp
            lib/v4l2_isp_camera.cpp
            lib/frame_pairer.cpp
            lib/composite_processor.cpp
            lib/encoded_packet_fanout.cpp)
        target_include_directories(myDemo PRIVATE
            ${PROJECT_ROOT}/include
            ${RKNN_API_PATH}/include
            ${MPP_PATH}/include
            ${RGA_PATH}/include
            ${OPENCV_ROOT}/include
            ${OPENCV_ROOT}/include/opencv4)
        target_link_libraries(myDemo PRIVATE
            ${OpenCV_LIBS_FILES}
            ${MPP_PATH}/lib/librockchip_mpp.so
            ${RGA_PATH}/lib/librga.so
            ${RKNN_API_PATH}/lib/librknn_api.so)
        set_target_properties(myDemo PROPERTIES
            BUILD_RPATH "${OPENCV_ROOT}/lib;${RKNN_API_PATH}/lib;${MPP_PATH}/lib;${RGA_PATH}/lib;/usr/lib/aarch64-linux-gnu"
            INSTALL_RPATH "${OPENCV_ROOT}/lib;${RKNN_API_PATH}/lib;${MPP_PATH}/lib;${RGA_PATH}/lib;/usr/lib/aarch64-linux-gnu")
    endif()
elseif(BUILD_RK_DEMO AND NOT UNIX)
    message(WARNING "RK demo requires Linux V4L2/MPP/RGA/RKNN userspace; only host tests are available on this host")
endif()
