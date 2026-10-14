# BomberDude: a 3D arena bomber. Eight themed arenas, 1-4 players, CPU
# rivals, Battle and Adventure. The rules (sim.c) are integer only.
#   bomberdude           the game
#   bomberdude_auto      plays the gate's pad script by itself (for hosts that send no input)
#   bomberdude_unlocked  starts with every Adventure stage beaten (all eight arenas in Battle)
set(SRC main.c sim.c ai.c world.c models.c fx.c scenery.c textures.c)
set(CW_PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/programs/bomberdude.txt)
cw_add_app(bomberdude SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
cw_add_app(bomberdude_auto SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
target_compile_definitions(bomberdude_auto PRIVATE BOMBERDUDE_AUTOPILOT=1)
cw_add_app(bomberdude_unlocked SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
target_compile_definitions(bomberdude_unlocked PRIVATE BOMBERDUDE_PRESET=8)
add_dependencies(bomberdude_auto_pack bomberdude_pack)
add_dependencies(bomberdude_unlocked_pack bomberdude_auto_pack)
