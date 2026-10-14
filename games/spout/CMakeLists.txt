# Spout: a paper spider climbs a rain spout. A weather cycle, slick paper
# wraps, and waves of water to brace against.
#   spout       the game
#   spout_auto  climbs by itself (for hosts that send no input)
set(SRC main.c sim.c levels.c world.c textures.c)
set(CW_PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/programs/spout.txt)
cw_add_app(spout SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
cw_add_app(spout_auto SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
target_compile_definitions(spout_auto PRIVATE SPOUT_AUTOPILOT=1)
add_dependencies(spout_auto_pack spout_pack)
