cmake_minimum_required(VERSION 3.16)
project(rk_camera_isp LANGUAGES CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
find_package(Threads REQUIRED)
link_libraries(Threads::Threads)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT MSVC)
    add_compile_options(-Wall -Wextra -Wpedantic)
else()
    add_compile_options(/W4 /utf-8)
endif()

set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
option(BUILD_HOST_TESTS "Build dependency-free lifecycle and CLI tests" ON)
option(BUILD_BSP_TOOLS "Build read-only media graph and rkAIQ status tools" ON)
option(BUILD_RK_DEMO "Build the Rockchip dual-camera MIPI application" OFF)
option(REQUIRE_RK_DEMO "Fail configure when RK demo dependencies are missing" OFF)
option(BUILD_SOFTWARE_DEMO "Build the dependency-light VirtualSensor + Software ISP demo" ON)
option(ENABLE_ASAN_UBSAN "Enable AddressSanitizer and UndefinedBehaviorSanitizer for host targets" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer for host targets" OFF)
option(ENABLE_ASAN "Enable standalone AddressSanitizer (including MSVC)" OFF)

if(ENABLE_ASAN)
    if(ENABLE_TSAN OR ENABLE_ASAN_UBSAN)
        message(FATAL_ERROR "Use a separate directory for standalone ASan")
    endif()
    if(MSVC)
        string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
        add_compile_options(/fsanitize=address)
        add_link_options(/INCREMENTAL:NO)
    else()
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address)
    endif()
endif()

if(ENABLE_TSAN AND ENABLE_ASAN_UBSAN)
    message(FATAL_ERROR "TSan and ASan/UBSan must use separate build directories")
endif()
if(MSVC AND (ENABLE_ASAN_UBSAN OR ENABLE_TSAN))
    message(FATAL_ERROR "Combined ASan/UBSan and TSan require a supported GCC/Clang toolchain; use the Linux host workflow")
endif()
if(ENABLE_ASAN_UBSAN AND NOT MSVC)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()
if(ENABLE_TSAN AND NOT MSVC)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()


option(ENABLE_V4L2 "Build real Linux V4L2 capture" ${BUILD_RK_DEMO})
option(ENABLE_ROCKCHIP_RGA "Enable SDK RGA boundary" ${BUILD_RK_DEMO})
option(ENABLE_ROCKCHIP_MPP "Enable SDK MPP boundary" ${BUILD_RK_DEMO})
option(ENABLE_RKNN "Enable SDK RKNN boundary" ${BUILD_RK_DEMO})
option(ENABLE_RKAIQ "Enable a verified BSP RkAiq SDK adapter" OFF)
add_library(runtime_core STATIC src/runtime/monitor.cpp)
target_include_directories(runtime_core PUBLIC ${PROJECT_ROOT}/include)
add_library(camera_core STATIC src/camera_frame.cpp)
target_include_directories(camera_core PUBLIC ${PROJECT_ROOT}/include)
add_library(model_contract STATIC src/ai/model_contract.cpp)
target_include_directories(model_contract PUBLIC ${PROJECT_ROOT}/include)
target_link_libraries(model_contract PUBLIC camera_core)
add_library(behavior_core STATIC src/behavior/event_manager.cpp)
target_include_directories(behavior_core PUBLIC ${PROJECT_ROOT}/include)
add_library(output_core STATIC src/output/packet_sink.cpp src/output/recording_index.cpp)
target_include_directories(output_core PUBLIC ${PROJECT_ROOT}/include)
set(RKNN_API_PATH ${PROJECT_ROOT}/../rknn CACHE PATH "RKNN SDK root")
set(MPP_PATH ${PROJECT_ROOT}/../mpp CACHE PATH "MPP SDK root")
set(RGA_PATH ${PROJECT_ROOT}/../rga CACHE PATH "RGA SDK root")
if(ENABLE_V4L2 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "ENABLE_V4L2 requires Linux V4L2 headers and devices")
endif()
if(ENABLE_ROCKCHIP_RGA)
    find_path(RGA_INCLUDE_DIR im2d.h HINTS ${RGA_PATH}/include PATH_SUFFIXES rga)
    find_library(RGA_LIBRARY rga HINTS ${RGA_PATH}/lib)
    if(NOT RGA_INCLUDE_DIR OR NOT RGA_LIBRARY)
        message(FATAL_ERROR "ENABLE_ROCKCHIP_RGA requires actual im2d.h and librga; provide RGA_PATH")
    endif()
endif()
if(ENABLE_ROCKCHIP_MPP)
    find_path(MPP_INCLUDE_DIR rk_mpi.h HINTS ${MPP_PATH}/include PATH_SUFFIXES rockchip)
    find_library(MPP_LIBRARY rockchip_mpp HINTS ${MPP_PATH}/lib)
    if(NOT MPP_INCLUDE_DIR OR NOT MPP_LIBRARY)
        message(FATAL_ERROR "ENABLE_ROCKCHIP_MPP requires actual rk_mpi.h and librockchip_mpp; provide MPP_PATH")
    endif()
endif()
if(ENABLE_RKNN)
    find_path(RKNN_INCLUDE_DIR rknn_api.h HINTS ${RKNN_API_PATH}/include)
    find_library(RKNN_LIBRARY NAMES rknnrt rknn_api HINTS ${RKNN_API_PATH}/lib)
    if(NOT RKNN_INCLUDE_DIR OR NOT RKNN_LIBRARY)
        message(FATAL_ERROR "RKNN_BACKEND_UNAVAILABLE: provide actual RKNN runtime headers/library via RKNN_API_PATH")
    endif()
endif()
add_library(isp_discovery STATIC
    src/isp/isp_types.cpp
    src/isp/isp_config.cpp
    src/isp/media_graph_inspector.cpp
    src/isp/sensor_registry.cpp
    src/isp/rkisp_capability.cpp
    src/media_graph_validator.cpp)
target_include_directories(isp_discovery PUBLIC ${PROJECT_ROOT}/include)
add_library(isp_control STATIC
    src/aiq_controller.cpp
    src/isp/isp_cli.cpp
    src/isp/rkaiq_controller.cpp
    src/isp/backends/external_rkaiq_service_backend.cpp
    src/isp/backends/rockchip_rkaiq_backend.cpp
    src/isp/isp_readiness.cpp
    src/isp/isp_health_monitor.cpp)
target_include_directories(isp_control PUBLIC ${PROJECT_ROOT}/include)
target_link_libraries(isp_control PUBLIC isp_discovery)
include(cmake/RkAiqAdapter.cmake)
configure_rkaiq_adapter(isp_control)
if(ENABLE_V4L2)
    add_library(v4l2_capture STATIC src/v4l2_isp_camera.cpp)
    target_include_directories(v4l2_capture PUBLIC ${PROJECT_ROOT}/include)
    target_link_libraries(v4l2_capture PUBLIC camera_core)
endif()

add_library(software_isp STATIC
    src/isp/isp_pipeline.cpp
    src/isp/ae_controller.cpp
    src/isp/isp_profile.cpp)
target_include_directories(software_isp PUBLIC ${PROJECT_ROOT}/include)

add_library(virtual_camera STATIC
    src/camera/raw10.cpp
    src/camera/virtual_sensor.cpp
    src/camera/virtual_sensor_config.cpp)
target_include_directories(virtual_camera PUBLIC ${PROJECT_ROOT}/include)

add_library(demo_ai STATIC src/ai/inference_backend.cpp)
target_include_directories(demo_ai PUBLIC ${PROJECT_ROOT}/include)
target_link_libraries(demo_ai PUBLIC software_isp)

if(BUILD_SOFTWARE_DEMO)
    add_executable(camera_isp_demo
        tools/camera_isp_demo.cpp
        src/frame_pairer.cpp
        src/composite_processor.cpp)
    target_include_directories(camera_isp_demo PRIVATE ${PROJECT_ROOT}/include)
    target_link_libraries(camera_isp_demo PRIVATE virtual_camera software_isp demo_ai)
endif()

if(BUILD_HOST_TESTS)
    enable_testing()
    if(MSVC)
        add_compile_options(/UNDEBUG)
    else()
        add_compile_options(-UNDEBUG)
    endif()
    add_executable(isp_discovery_test tests/isp_discovery_test.cpp)
    target_link_libraries(isp_discovery_test PRIVATE isp_discovery)
    target_compile_definitions(isp_discovery_test PRIVATE ISP_TEST_FIXTURE_DIR="${PROJECT_ROOT}/tests/fixtures/isp")
    add_test(NAME isp_discovery_test COMMAND isp_discovery_test)
    add_executable(rkaiq_lifecycle_test tests/rkaiq_lifecycle_test.cpp)
    target_link_libraries(rkaiq_lifecycle_test PRIVATE isp_control)
    add_test(NAME rkaiq_lifecycle_test COMMAND rkaiq_lifecycle_test)
    add_executable(isp_health_monitor_test tests/isp_health_monitor_test.cpp)
    target_link_libraries(isp_health_monitor_test PRIVATE isp_control)
    add_test(NAME isp_health_monitor_test COMMAND isp_health_monitor_test)
    add_executable(isp_readiness_test tests/isp_readiness_test.cpp)
    target_link_libraries(isp_readiness_test PRIVATE isp_control)
    target_compile_definitions(isp_readiness_test PRIVATE ISP_TEST_FIXTURE_DIR="${PROJECT_ROOT}/tests/fixtures/isp")
    add_test(NAME isp_readiness_test COMMAND isp_readiness_test)
    add_executable(runtime_monitor_test tests/runtime_monitor_test.cpp)
    target_link_libraries(runtime_monitor_test PRIVATE runtime_core)
    add_test(NAME runtime_monitor_test COMMAND runtime_monitor_test)
    add_executable(camera_queue_test tests/camera_queue_test.cpp src/frame_pairer.cpp)
    target_link_libraries(camera_queue_test PRIVATE camera_core)
    add_test(NAME camera_queue_test COMMAND camera_queue_test)
    set_tests_properties(camera_queue_test PROPERTIES TIMEOUT 30 LABELS "stress;unit")
    add_executable(model_contract_test tests/model_contract_test.cpp)
    target_link_libraries(model_contract_test PRIVATE model_contract)
    add_test(NAME model_contract_test COMMAND model_contract_test)
    add_executable(behavior_event_test tests/behavior_event_test.cpp)
    target_link_libraries(behavior_event_test PRIVATE behavior_core)
    add_test(NAME behavior_event_test COMMAND behavior_event_test)
    set_tests_properties(behavior_event_test PROPERTIES TIMEOUT 30 LABELS "stress;unit")
    add_executable(software_integration_test tests/software_integration_test.cpp src/frame_pairer.cpp)
    target_link_libraries(software_integration_test PRIVATE camera_core behavior_core output_core)
    add_test(NAME software_integration_test COMMAND software_integration_test)
    set_tests_properties(software_integration_test PROPERTIES TIMEOUT 30 LABELS integration)
    add_executable(output_pipeline_test tests/output_pipeline_test.cpp src/encoded_packet_fanout.cpp)
    target_link_libraries(output_pipeline_test PRIVATE output_core)
    add_test(NAME output_pipeline_test COMMAND output_pipeline_test)
    set_tests_properties(output_pipeline_test PROPERTIES TIMEOUT 30 LABELS "stress;unit")
    add_executable(isp_host_tests
        tests/camera_lifecycle_test.cpp
        src/app_options.cpp
        src/composite_processor.cpp)
    target_include_directories(isp_host_tests PRIVATE ${PROJECT_ROOT}/include)
    add_test(NAME isp_host_tests COMMAND isp_host_tests)
    add_executable(composite_processor_test
        tests/composite_processor_test.cpp
        src/composite_processor.cpp)
    target_include_directories(composite_processor_test PRIVATE ${PROJECT_ROOT}/include)
    add_test(NAME composite_processor_test COMMAND composite_processor_test)
    add_executable(encoded_packet_fanout_test
        tests/encoded_packet_fanout_test.cpp
        src/encoded_packet_fanout.cpp)
    target_include_directories(encoded_packet_fanout_test PRIVATE ${PROJECT_ROOT}/include)
    add_test(NAME encoded_packet_fanout_test COMMAND encoded_packet_fanout_test)
    add_executable(frame_pairer_test
        tests/frame_pairer_test.cpp
        src/frame_pairer.cpp)
    target_include_directories(frame_pairer_test PRIVATE ${PROJECT_ROOT}/include)
    add_test(NAME frame_pairer_test COMMAND frame_pairer_test)
    add_executable(media_graph_validator_test tests/media_graph_validator_test.cpp)
    target_link_libraries(media_graph_validator_test PRIVATE isp_discovery)
    target_compile_definitions(media_graph_validator_test PRIVATE ISP_TEST_FIXTURE_DIR="${PROJECT_ROOT}/tests/fixtures/isp")
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
                 COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${PROJECT_ROOT}/tests -p "*isp_quality_analyzer*.py")
        add_test(NAME event_video_association_test
                 COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${PROJECT_ROOT}/tests -p "test_associate_events.py")
        add_test(NAME isp_diagnostics_test
                 COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${PROJECT_ROOT}/tests -p "test_isp_diagnostics.py")
        add_test(NAME rkaiq_sdk_detection_test
                 COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${PROJECT_ROOT}/tests -p "test_rkaiq_cmake.py")
        set_tests_properties(rkaiq_sdk_detection_test PROPERTIES ENVIRONMENT "RKISP_TEST_CMAKE=${CMAKE_COMMAND}")
    endif()
endif()

if(BUILD_HOST_TESTS)
    get_property(_host_tests DIRECTORY PROPERTY TESTS)
    set_tests_properties(${_host_tests} PROPERTIES TIMEOUT 30)
endif()

if(BUILD_BSP_TOOLS)
    add_executable(media_graph_validator tools/media_graph_validator_main.cpp)
    target_link_libraries(media_graph_validator PRIVATE isp_discovery)
    target_include_directories(media_graph_validator PRIVATE ${PROJECT_ROOT}/include)

    add_executable(isp_control_cli tools/isp_control_cli.cpp)
    target_link_libraries(isp_control_cli PRIVATE isp_control)
    target_include_directories(isp_control_cli PRIVATE ${PROJECT_ROOT}/include)

    add_executable(isp_debug_tool tools/isp_debug_tool.cpp)
    target_link_libraries(isp_debug_tool PRIVATE isp_control)
    target_include_directories(isp_debug_tool PRIVATE ${PROJECT_ROOT}/include)
    if(BUILD_HOST_TESTS AND Python3_Interpreter_FOUND)
        add_test(NAME isp_control_cli_test
                 COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${PROJECT_ROOT}/tests -p "test_isp_cli.py")
        set_tests_properties(isp_control_cli_test PROPERTIES TIMEOUT 30
            ENVIRONMENT "ISP_CONTROL_CLI=$<TARGET_FILE:isp_control_cli>;ISP_DEBUG_TOOL=$<TARGET_FILE:isp_debug_tool>")
        add_test(NAME media_graph_cli_test
                 COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${PROJECT_ROOT}/tests -p "test_media_graph_cli.py")
        set_tests_properties(media_graph_cli_test PROPERTIES TIMEOUT 30
            ENVIRONMENT "MEDIA_GRAPH_VALIDATOR=$<TARGET_FILE:media_graph_validator>")
    endif()
endif()

if(BUILD_RK_DEMO)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "myDemo requires Linux/RK3588 V4L2; use BUILD_RK_DEMO=OFF for host tests")
    endif()
    if(NOT ENABLE_V4L2 OR NOT ENABLE_ROCKCHIP_RGA OR NOT ENABLE_ROCKCHIP_MPP OR NOT ENABLE_RKNN)
        message(FATAL_ERROR "Existing myDemo needs V4L2/RGA/MPP/RKNN adapters enabled. No SDK-free hardware stub is provided.")
    endif()
    set(OPENCV_ROOT ${PROJECT_ROOT}/../opencv4-1 CACHE PATH "OpenCV SDK root")
    find_package(OpenCV QUIET COMPONENTS core imgproc imgcodecs)
    if(NOT OpenCV_FOUND)
        find_path(OpenCV_INCLUDE_DIRS opencv2/core.hpp HINTS ${OPENCV_ROOT}/include/opencv4)
        file(GLOB OpenCV_LIBS "${OPENCV_ROOT}/lib/libopencv_*.so")
        if(NOT OpenCV_INCLUDE_DIRS OR NOT OpenCV_LIBS)
            message(FATAL_ERROR "myDemo needs matching BSP OpenCV headers/libraries; provide OpenCV_DIR or OPENCV_ROOT")
        endif()
    endif()
    add_executable(myDemo
        src/main.cpp
        src/app_options.cpp
        src/dual_camera_pipeline.cpp
        src/mpp_encoder.cpp
        src/postprocess.cpp
        src/v4l2_isp_camera.cpp
        src/frame_pairer.cpp
        src/composite_processor.cpp
        src/encoded_packet_fanout.cpp)
    target_include_directories(myDemo PRIVATE
        ${PROJECT_ROOT}/include ${RKNN_INCLUDE_DIR} ${MPP_INCLUDE_DIR} ${RGA_INCLUDE_DIR} ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(myDemo PRIVATE
        camera_core model_contract behavior_core output_core runtime_core isp_control
        ${OpenCV_LIBS} ${MPP_LIBRARY} ${RGA_LIBRARY} ${RKNN_LIBRARY})
elseif(REQUIRE_RK_DEMO)
    message(FATAL_ERROR "REQUIRE_RK_DEMO needs BUILD_RK_DEMO=ON")
endif()
