# racer: an arcade kart racer on Box3D. Eight maps, one to four players, loops, corkscrews, jumps.
#   racer       the game
#   racer_auto  eight CPU drivers, by itself (for hosts that send no input)
set(SRC main.c ui.c sim.c ai.c world.c models.c models_veh.c kartgfx.c items_gfx.c fx.c fx_amb.c audio.c textures.c props.c propgen_nature.c propgen_man.c dressing.c ground.c track.c trackmesh.c maps.c maps/m0_test.c
    maps/m1_salt.c maps/m2_drowned.c maps/m3_fair.c maps/m4_refinery.c maps/m5_dam.c maps/m6_frost.c maps/m7_temple.c maps/m8_orbit.c)
set(CW_PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/programs/racer.txt)
set(PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
cw_add_app(racer SOURCES ${SRC} PACK ${PACK})
cw_add_app(racer_auto SOURCES ${SRC} PACK ${PACK})
target_compile_definitions(racer_auto PRIVATE RACER_AUTOPILOT=1)
add_dependencies(racer_auto_pack racer_pack)
