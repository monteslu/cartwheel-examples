# Otium: Roman trade across Italy against two rival houses; time moves only
# while you act. The rules (sim.c) are integers, the rivals in ai.c.
#   otium       the game
#   otium_auto  plays itself (for hosts that send no input)
set(SRC main.c sim.c ai.c map.c world.c textures.c)
set(CW_PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/programs/otium.txt)
cw_add_app(otium SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
cw_add_app(otium_auto SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
target_compile_definitions(otium_auto PRIVATE OTIUM_AUTOPILOT=1)
add_dependencies(otium_auto_pack otium_pack)
