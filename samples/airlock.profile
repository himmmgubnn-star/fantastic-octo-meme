# Example Airlock/repository compatibility profile.
# Import with: airlock profile import "My Game" samples/airlock.profile
name: Example Game
label: example
runner: airlock-wine-10.x
architecture: x86
windows_version: win10
graphics.backend: vulkan
audio.backend: alsa
runtime: box64_preset=performance; esync=1
dependencies:
  - vcruntime
  - directx
dll_overrides: d3d11=native,builtin
windows.resolution: 1280x720
windows.virtual_desktop: 1
launch.executable: drive_c/Games/ExampleGame/game.exe
launch.arguments: -fullscreen
trust: official
source: airlock-curated
version: 1
