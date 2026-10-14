# The games, built by cartwheel-engine's CMakeLists.txt as its project
# (CW_PROJECT_DIR = this directory, CW_ENGINE_ONLY = ON). scripts/build.sh
# configures it; by hand:
#
#   cmake -S deps/cartwheel-engine -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Release \
#     -DCW_ENGINE_ONLY=ON -DCW_PROJECT_DIR=$PWD
#
# Each game declares its apps with cw_add_app in games/<name>/CMakeLists.txt.
if(NOT COMMAND cw_add_app)
  message(FATAL_ERROR "configure cartwheel-engine with -DCW_PROJECT_DIR=<this directory> (scripts/build.sh does it); "
    "this file is not a top-level project")
endif()

foreach(game bikemesa bomberdude derby pugball spout)
  add_subdirectory(games/${game})
endforeach()
