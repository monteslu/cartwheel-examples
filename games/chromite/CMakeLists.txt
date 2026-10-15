# Chromite: a vertical shooter over a chrome fortress, after Chromium B.S.U.
# (2000): its rules, waves and craft, rebuilt in three.c.
#   chromite       the game
#   chromite_auto  flies itself (for hosts that send no input)
set(SRC main.c sim.c script.c world.c models.c fx.c textures.c)
set(CW_PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/programs/chromite.txt)
cw_add_app(chromite SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
cw_add_app(chromite_auto SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
target_compile_definitions(chromite_auto PRIVATE CHROMITE_AUTOPILOT=1)
add_dependencies(chromite_auto_pack chromite_pack)
