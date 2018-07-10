cmake_minimum_required(VERSION 3.8)
project(H264CameraServer)

set(CMAKE_CXX_STANDARD 11)

include_directories(C:\\msys64\\mingw64\\include)
include_directories(C:\\msys64\\mingw64\\include\\glib-2.0)
include_directories(C:\\msys64\\mingw64\\lib\\glib-2.0\\include)
include_directories(C:\\msys64\\mingw64\\include\\gstreamer-1.0)

link_directories(C:\\msys64\\mingw64\\lib)
link_directories(C:\\msys64\\mingw64\\lib\\glib-2.0)
link_directories(C:\\msys64\\mingw64\\lib\\gstreamer-1.0)

set(SOURCE_FILES main.cpp Logger.hpp CameraDevice.cpp CameraDevice.hpp)
find_package(PkgConfig)
pkg_check_modules(GST REQUIRED gstreamer-1.0>=1.4
        gstreamer-rtsp-server-1.0>=1.4 )
pkg_check_modules(FFMPEG REQUIRED libavdevice libavformat libavfilter libavcodec libswscale libavutil )

add_executable(H264CameraServer ${SOURCE_FILES})

target_link_libraries(
        H264CameraServer
        ws2_32
        pthread
        boost_log-mt
        ${GST_LIBRARIES}
        ${FFMPEG_LIBRARIES}
)