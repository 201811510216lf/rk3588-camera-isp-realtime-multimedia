cmake_minimum_required(VERSION 3.16)
project(rk_platform LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
endif()

set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
option(BUILD_HOST_TESTS "Build dependency-free lifecycle and CLI tests" ON)
option(BUILD_RK_DEMO "Build the Rockchip/FFmpeg/OpenCV application" ${UNIX})
option(REQUIRE_RK_DEMO "Fail configure when RK demo dependencies are missing" OFF)

if(BUILD_HOST_TESTS)
    enable_testing()
    add_executable(isp_host_tests
        tests/camera_lifecycle_test.cpp
        lib/app_options.cpp
    )
    target_include_directories(isp_host_tests PRIVATE ${PROJECT_ROOT}/include)
    add_test(NAME isp_host_tests COMMAND isp_host_tests)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME project_structure_validation
                 COMMAND ${Python3_EXECUTABLE} ${PROJECT_ROOT}/tests/validate_project.py)
    endif()
endif()

if(BUILD_RK_DEMO)
    if(NOT UNIX)
        message(WARNING "RK demo requires Linux V4L2/MPP/RGA/RKNN userspace; it will not be built on this host")
    else()
        # ================= Rockchip and media SDK paths =================
        set(RKNN_API_PATH ${PROJECT_ROOT}/../rknn)
        set(MPP_PATH      ${PROJECT_ROOT}/../mpp)
        set(RGA_PATH      ${PROJECT_ROOT}/../rga)
        set(FFMPEG_ROOT   ${PROJECT_ROOT}/../ffmpeg-lite)
        set(OPENCV_ROOT   ${PROJECT_ROOT}/../opencv4-1)

        set(RK_DEMO_DEPS_OK TRUE)
        foreach(required_path IN ITEMS
            ${RKNN_API_PATH}/include
            ${RKNN_API_PATH}/lib/librknn_api.so
            ${RKNN_API_PATH}/lib/librknnrt.so
            ${MPP_PATH}/include
            ${MPP_PATH}/lib/librockchip_mpp.so
            ${RGA_PATH}/include
            ${RGA_PATH}/lib/librga.so
            ${FFMPEG_ROOT}/include
            ${FFMPEG_ROOT}/lib
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
            set(FFMPEG_INCLUDE_DIR ${FFMPEG_ROOT}/include)
            set(FFMPEG_LIB_DIR ${FFMPEG_ROOT}/lib)
            set(OpenCV_LIBS ${OpenCV_LIBS_FILES})

            include_directories(
                ${PROJECT_ROOT}
                ${PROJECT_ROOT}/include
                ${RKNN_API_PATH}/include
                ${MPP_PATH}/include
                ${RGA_PATH}/include
                ${FFMPEG_INCLUDE_DIR}
                ${OPENCV_ROOT}/include
                ${OPENCV_ROOT}/include/opencv4
            )

            file(GLOB DEMO_LIB_SOURCES CONFIGURE_DEPENDS ${PROJECT_ROOT}/lib/*.cpp)
            add_executable(myDemo main.cpp ${DEMO_LIB_SOURCES})
            target_link_libraries(myDemo
                ${FFMPEG_LIB_DIR}/libavcodec.so
                ${FFMPEG_LIB_DIR}/libavformat.so
                ${FFMPEG_LIB_DIR}/libavutil.so
                ${OpenCV_LIBS}
                ${MPP_PATH}/lib/librockchip_mpp.so
                ${RGA_PATH}/lib/librga.so
                ${RKNN_API_PATH}/lib/librknn_api.so
                ${RKNN_API_PATH}/lib/librknnrt.so
            )
            set_target_properties(myDemo PROPERTIES
                BUILD_RPATH "${FFMPEG_LIB_DIR};${OPENCV_ROOT}/lib;${RKNN_API_PATH}/lib;${MPP_PATH}/lib;${RGA_PATH}/lib;/usr/lib/aarch64-linux-gnu"
                INSTALL_RPATH "${FFMPEG_LIB_DIR};${OPENCV_ROOT}/lib;${RKNN_API_PATH}/lib;${MPP_PATH}/lib;${RGA_PATH}/lib;/usr/lib/aarch64-linux-gnu"
            )
        endif()
    endif()
endif()
