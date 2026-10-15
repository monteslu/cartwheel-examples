# The games. Built two ways (scripts/build.sh does either):
#  - against a prebuilt engine SDK, with this file at the top of the build:
#      cmake -S . -B build/linux-x64 -G Ninja -DCMAKE_BUILD_TYPE=Release \
#        -DCMAKE_PREFIX_PATH=deps/cartwheel-sdk/linux-x64
#  - with the engine from source (CARTWHEEL_ENGINE_SOURCE=1), as the engine's
#    project (CW_PROJECT_DIR = this directory, CW_ENGINE_ONLY = ON):
#      cmake -S deps/cartwheel-engine -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Release \
#        -DCW_ENGINE_ONLY=ON -DCW_PROJECT_DIR=$PWD
#
# Each game declares its apps with cw_add_app in games/<name>/CMakeLists.txt.
if(NOT COMMAND cw_add_app)
  # the top of the build: the games against a prebuilt engine SDK
  # (scripts/fetch-deps.sh fetches it; scripts/build.sh configures this)
  cmake_minimum_required(VERSION 3.20)
  project(cartwheel_examples C CXX)
  find_package(cartwheel REQUIRED CONFIG)
endif()

foreach(game bikemesa bomberdude derby pugball spout)
  add_subdirectory(games/${game})
endforeach()
