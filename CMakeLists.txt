cmake_minimum_required(VERSION 3.10)
project(vita-plex-client C)

# Windows: cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=C:/Users/steve/vita-dev/vitasdk/vitasdk/share/vita.toolchain.cmake -B build
set(VITA_TITLEID "PLEX00001")
set(VITA_TITLE "Plex Client")
set(VITA_VERSION "01.38")

include("${VITASDK}/share/vita.cmake" REQUIRED)

add_executable(${PROJECT_NAME}
  src/performance.c
  src/touch.c
  src/text.c
  src/library.c
  src/network.c
  src/progress.c
  src/video.c
  src/session.c
  src/main.c
  src/plex.c
  src/plex_auth.c
  src/browse.c
  src/settings.c
  src/http.c
  src/certs.c
  src/player.c
  src/hls.c
  src/hls_backend.c
  src/update.c
  src/gui.c
  src/debugScreen.c
)

target_include_directories(${PROJECT_NAME} PRIVATE src)
set(PLEX_FFMPEG_ROOT "${CMAKE_SOURCE_DIR}/build/deps/ffmpeg-vita" CACHE PATH "Pinned Vita stream codecs")
if(NOT EXISTS "${PLEX_FFMPEG_ROOT}/lib/libavcodec.a")
  message(FATAL_ERROR "Build the streaming codecs first: bash tools/build-stream-codecs.sh (VITASDK must be set)")
endif()
target_include_directories(${PROJECT_NAME} PRIVATE "${PLEX_FFMPEG_ROOT}/include")
# Newer binutils emits RELRO segments that vita-elf-create can't map;
# disable them to keep the classic text/data two-segment layout.
target_link_options(${PROJECT_NAME} PRIVATE "LINKER:-z,norelro")
# Optimize for size and drop unused sections. check-elf.py validates the final
# SDK-generated code/data layout, including the stream codec archives.
target_compile_options(${PROJECT_NAME} PRIVATE -Os -Wall -Wextra -Wno-misleading-indentation -Wno-unused-parameter -ffunction-sections -fdata-sections)
target_link_options(${PROJECT_NAME} PRIVATE "LINKER:--gc-sections")
target_link_libraries(${PROJECT_NAME}
  "${PLEX_FFMPEG_ROOT}/lib/libavformat.a"
  "${PLEX_FFMPEG_ROOT}/lib/libavcodec.a"
  "${PLEX_FFMPEG_ROOT}/lib/libavutil.a"
  pthread m
  SceAudiodec_stub
  SceVideodec_stub
  SceCodecEngine_stub
  SceSysmem_stub
  SceTouch_stub
  ScePower_stub
  SceDisplay_stub
  SceCtrl_stub
  SceSysmodule_stub
  SceNet_stub
  SceNetCtl_stub
  SceIme_stub
  SceGxm_stub
  SceCommonDialog_stub
  SceAppUtil_stub
  SceHttp_stub
  SceSsl_stub
  SceAudio_stub
  ScePromoterUtil_stub
  SceAvPlayer_stub
  SceLibKernel_stub
)

vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME} UNSAFE)
vita_create_vpk(vita-plex-client-${VITA_VERSION}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_TITLE}
  FILE ${CMAKE_SOURCE_DIR}/sce_sys/icon0.png sce_sys/icon0.png
  FILE ${CMAKE_SOURCE_DIR}/sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
  FILE ${CMAKE_SOURCE_DIR}/sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE ${CMAKE_SOURCE_DIR}/sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
  FILE ${CMAKE_SOURCE_DIR}/assets/font16.bin assets/font16.bin
  FILE ${CMAKE_SOURCE_DIR}/assets/font20.bin assets/font20.bin
  FILE ${CMAKE_SOURCE_DIR}/assets/font28.bin assets/font28.bin
  FILE ${CMAKE_SOURCE_DIR}/assets/FONT-LICENSE.txt assets/FONT-LICENSE.txt
  FILE ${CMAKE_SOURCE_DIR}/assets/STREAM-CODECS-NOTICES.txt assets/STREAM-CODECS-NOTICES.txt
  FILE ${CMAKE_SOURCE_DIR}/assets/FFmpeg-LGPL-2.1.txt assets/FFmpeg-LGPL-2.1.txt
  FILE ${CMAKE_SOURCE_DIR}/assets/wiliwili-GPL-3.0.txt assets/wiliwili-GPL-3.0.txt
)
