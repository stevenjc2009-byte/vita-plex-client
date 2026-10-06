cmake_minimum_required(VERSION 3.10)
project(vita-plex-client C)

# Windows: cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=C:/Users/steve/vita-dev/vitasdk/vitasdk/share/vita.toolchain.cmake -B build
set(VITA_TITLEID "PLEX00001")
set(VITA_TITLE "Plex Client")
set(VITA_VERSION "01.01")

include("${VITASDK}/share/vita.cmake" REQUIRED)

add_executable(${PROJECT_NAME}
  src/main.c
  src/plex.c
  src/plex_auth.c
  src/browse.c
  src/settings.c
  src/http.c
  src/player.c
  src/update.c
  src/debugScreen.c
)

target_include_directories(${PROJECT_NAME} PRIVATE src)
# Newer binutils emits RELRO segments that vita-elf-create can't map;
# disable them to keep the classic text/data two-segment layout.
target_link_options(${PROJECT_NAME} PRIVATE "LINKER:-z,norelro")
target_link_libraries(${PROJECT_NAME}
  SceDisplay_stub
  SceCtrl_stub
  SceSysmodule_stub
  SceNet_stub
  SceNetCtl_stub
  SceHttp_stub
  SceSsl_stub
  SceAudio_stub
  ScePromoterUtil_stub
  SceAvPlayer_stub
  SceLibKernel_stub
)

vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME} UNSAFE)
vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_TITLE}
)
