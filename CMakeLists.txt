cmake_minimum_required(VERSION 3.10)
project(airlock C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# Sources shared by the library, the CLI, and the tests.
# ---------------------------------------------------------------------------
set(AIRLOCK_LIB_SOURCES
  src/loader/airlock_util.c
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
  src/workspace/workspace.c
)
if(AIRLOCK_AUDIO_ALSA)
  list(APPEND AIRLOCK_LIB_SOURCES src/audio/alsa.c)
endif()

set(AIRLOCK_LIB_HEADERS
  include/airlock/airlock.h
  include/airlock/pe.h
  include/airlock/loader.h
  include/airlock/win32.h
  include/airlock/platform.h
  include/airlock/audio.h
  include/airlock/perf.h
  include/airlock/compat.h
  include/airlock/timer.h
  include/airlock/sync.h
  include/airlock/shmem.h
  include/airlock/trace.h
  include/airlock/shadercache.h
  include/airlock/plugin.h
  include/airlock/crash.h
  include/airlock/inspect.h
  include/airlock/db.h
  include/airlock/runtime.h
  include/airlock/shell.h
  include/airlock/prefix.h
  include/airlock/display.h
  include/airlock/security.h
  include/airlock/desktop.h
  include/airlock/installer.h
  include/airlock/com.h
  include/airlock/service.h
  include/airlock/notify.h
  include/airlock/a11y.h
  include/airlock/locale.h
  include/airlock/print.h
  include/airlock/device.h
  include/airlock/debug.h
  include/airlock/testlab.h
  include/airlock/workspace.h
)

add_library(airlock STATIC ${AIRLOCK_LIB_SOURCES} ${AIRLOCK_LIB_HEADERS})
target_include_directories(airlock PUBLIC include)
target_link_libraries(airlock PUBLIC ${CMAKE_DL_LIBS})

if(AIRLOCK_AUDIO_ALSA)
  find_package(ALSA REQUIRED)
  target_link_libraries(airlock PUBLIC ALSA::ALSA)
  target_compile_definitions(airlock PUBLIC AIRLOCK_AUDIO_ALSA)
endif()

add_executable(airlock-cli src/cli.c)
target_link_libraries(airlock-cli PRIVATE airlock)

enable_testing()
foreach(t loader audio perf compat kit ecosystem workspace)
  add_executable(test_${t} tests/test_${t}.c)
  target_link_libraries(test_${t} PRIVATE airlock)
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
# On Android the CLI is built (via airlock-cli target name `airlock-cli`); a
# packaged app would link libairlock.a and call airlock_win32_init() itself.
# Audio defaults to the null/WAV sink on Android (no ALSA) and can later be
# replaced with an AAudio/Oboe backend.
# ---------------------------------------------------------------------------
