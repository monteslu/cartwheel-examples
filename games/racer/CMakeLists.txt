# racer: an arcade kart racer on Box3D. Eight maps, one to four players, loops, corkscrews, jumps.
#   racer       the game
#   racer_auto  eight CPU drivers, by itself (for hosts that send no input)
set(SRC main.c sim.c ai.c world.c models.c track.c trackmesh.c maps.c maps/m0_test.c)
set(CW_PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/programs/racer.txt)
set(PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
cw_add_app(racer SOURCES ${SRC} PACK ${PACK})
cw_add_app(racer_auto SOURCES ${SRC} PACK ${PACK})
target_compile_definitions(racer_auto PRIVATE RACER_AUTOPILOT=1)
add_dependencies(racer_auto_pack racer_pack)
