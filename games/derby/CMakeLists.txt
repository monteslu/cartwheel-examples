# Derby: a demolition derby. Box3D cars on cw_vehicle3d wheels in a walled
# dirt bowl, dents, smoke and fire, last car running wins.
#   derby       the game
#   derby_auto  six CPU drivers to a winner, by itself (for hosts that send no input)
#   derby_ram   parks car 2 broadside in front of yours and holds the field still (the ram gate)
set(SRC main.c sim.c ai.c world.c models.c cars.c fx.c scenery.c textures.c)
set(CW_PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/programs/derby.txt)
cw_add_app(derby SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
cw_add_app(derby_auto SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
target_compile_definitions(derby_auto PRIVATE DERBY_AUTOPILOT=1)
cw_add_app(derby_ram SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
target_compile_definitions(derby_ram PRIVATE DERBY_RAM_TEST=1)
add_dependencies(derby_auto_pack derby_pack)
add_dependencies(derby_ram_pack derby_auto_pack)
