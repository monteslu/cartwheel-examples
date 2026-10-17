# Pugball: soccer with jet-pack pugs and a fat, furious cat for a ball.
# 1v1 to 3v3, players and CPUs, five stadiums, the Pug Cup. The match
# (sim.c) runs on Box3D.
#   pugball           the game
#   pugball_auto      plays the gate's pad script by itself (for hosts that send no input)
#   pugball_unlocked  starts with the whole Cup won (the progression gates)
#   pugball_demo      the first kickoff parks the red pug in blue 1's path (the demolition gate)
#   pugball_cup       a fresh save with Cup matches cut to 6 s (wins fixture 1, unlocks stadium 2)
set(SRC main.c sim.c arena.c ai.c world.c net.c models.c fx.c scenery.c textures.c)
set(CW_PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/programs/pugball.txt)
set(PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
cw_add_app(pugball SOURCES ${SRC} PACK ${PACK})
cw_add_app(pugball_auto SOURCES ${SRC} PACK ${PACK})
target_compile_definitions(pugball_auto PRIVATE PUGBALL_AUTOPILOT=1)
cw_add_app(pugball_unlocked SOURCES ${SRC} PACK ${PACK})
target_compile_definitions(pugball_unlocked PRIVATE PUGBALL_PRESET=5)
cw_add_app(pugball_demo SOURCES ${SRC} PACK ${PACK})
target_compile_definitions(pugball_demo PRIVATE PUGBALL_DEMO_TEST=1)
cw_add_app(pugball_cup SOURCES ${SRC} PACK ${PACK})
target_compile_definitions(pugball_cup PRIVATE PUGBALL_CUP_TEST=1)
add_dependencies(pugball_auto_pack pugball_pack)
add_dependencies(pugball_unlocked_pack pugball_auto_pack)
add_dependencies(pugball_demo_pack pugball_unlocked_pack)
add_dependencies(pugball_cup_pack pugball_demo_pack)
