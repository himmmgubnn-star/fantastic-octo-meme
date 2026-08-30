cmake_minimum_required(VERSION 3.10)
project(cellar C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# Sources shared by the library, the CLI, and the tests.
# ---------------------------------------------------------------------------
set(CELLAR_LIB_SOURCES
  src/loader/cellar_util.c
  src/loader/pe.c
  src/loader/loader.c
  src/win32/api.c
  src/win32/mod_kernel32.c
  src/win32/mod_winmm.c
  src/win32/mod_user32.c
  src/win32/mod_advapi32.c
  src/win32/mod_shell32.c
  src/win32/mod_ole32.c
  src/win32/mod_comdlg32.c
  src/win32/mod_gdi32.c
  src/win32/mod_ws2_32.c
  src/win32/mod_version.c
  src/win32/mod_ntdll.c
  src/win32/init.c
  src/port/posix.c
  src/audio/audio.c
  src/perf/perf.c
  src/compat/compat.c
  src/compat/profile.c
  src/timer/timer.c
  src/sync/sync.c
  src/shmem/shmem.c
  src/trace/trace.c
  src/gfx/shadercache.c
  src/plugin/plugin.c
  src/crash/crash.c
  src/inspect/inspect.c
  src/db/db.c
  src/runtime/runtime.c
  src/shell/shell.c
  src/prefix/prefix.c
  src/display/display.c
  src/security/security.c
  src/desktop/desktop.c
  src/installer/installer.c
  src/com/com.c
  src/service/service.c
  src/notify/notify.c
  src/a11y/a11y.c
  src/locale/locale.c
  src/print/print.c
  src/device/device.c
  src/debug/debug.c
  src/testlab/testlab.c
)
if(CELLAR_AUDIO_ALSA)
  list(APPEND CELLAR_LIB_SOURCES src/audio/alsa.c)
endif()

set(CELLAR_LIB_HEADERS
  include/cellar/cellar.h
  include/cellar/pe.h
  include/cellar/loader.h
  include/cellar/win32.h
  include/cellar/platform.h
  include/cellar/audio.h
  include/cellar/perf.h
  include/cellar/compat.h
  include/cellar/timer.h
  include/cellar/sync.h
  include/cellar/shmem.h
  include/cellar/trace.h
  include/cellar/shadercache.h
  include/cellar/plugin.h
  include/cellar/crash.h
  include/cellar/inspect.h
  include/cellar/db.h
  include/cellar/runtime.h
  include/cellar/shell.h
  include/cellar/prefix.h
  include/cellar/display.h
  include/cellar/security.h
  include/cellar/desktop.h
  include/cellar/installer.h
  include/cellar/com.h
  include/cellar/service.h
  include/cellar/notify.h
  include/cellar/a11y.h
  include/cellar/locale.h
  include/cellar/print.h
  include/cellar/device.h
  include/cellar/debug.h
  include/cellar/testlab.h
)

add_library(cellar STATIC ${CELLAR_LIB_SOURCES} ${CELLAR_LIB_HEADERS})
target_include_directories(cellar PUBLIC include)
target_link_libraries(cellar PUBLIC ${CMAKE_DL_LIBS})

if(CELLAR_AUDIO_ALSA)
  find_package(ALSA REQUIRED)
  target_link_libraries(cellar PUBLIC ALSA::ALSA)
  target_compile_definitions(cellar PUBLIC CELLAR_AUDIO_ALSA)
endif()

add_executable(cellar-cli src/cli.c)
target_link_libraries(cellar-cli PRIVATE cellar)

enable_testing()
foreach(t loader audio perf compat kit ecosystem)
  add_executable(test_${t} tests/test_${t}.c)
  target_link_libraries(test_${t} PRIVATE cellar)
  add_test(NAME ${t} COMMAND test_${t})
endforeach()

# ---------------------------------------------------------------------------
# Android NDK support.
#
# Cross-compile with the NDK toolchain:
#   cmake -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
#         -DANDROID_ABI=arm64-v8a \
#         -DANDROID_PLATFORM=android-24 \
#         -B build-android
#   cmake --build build-android
#
# On Android the CLI is built (via cellar-cli target name `cellar-cli`); a
# packaged app would link libcellar.a and call cellar_win32_init() itself.
# Audio defaults to the null/WAV sink on Android (no ALSA) and can later be
# replaced with an AAudio/Oboe backend.
# ---------------------------------------------------------------------------
