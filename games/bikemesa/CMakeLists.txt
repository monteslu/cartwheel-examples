# BikeMesa: a downhill bike game. A 2D Box2D bicycle and rider through 3D
# worlds: jumps, flips, crashes, split screen for up to four players.
#   bikemesa           the game
#   bikemesa_auto      plays a session by itself (for hosts that send no input)
#   bikemesa_unlocked  starts with every track open (the progression gates)
set(SRC main.c bike.c crash.c models.c view.c textures.c backdrop.c)
set(CW_PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/programs/bikemesa.txt)
cw_add_app(bikemesa SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
cw_add_app(bikemesa_auto SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
target_compile_definitions(bikemesa_auto PRIVATE BIKEMESA_AUTOPILOT=1)
cw_add_app(bikemesa_unlocked SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
target_compile_definitions(bikemesa_unlocked PRIVATE BIKEMESA_PRESET=8)
# the packs share the import cache beside roots.txt: one after another
add_dependencies(bikemesa_auto_pack bikemesa_pack)
add_dependencies(bikemesa_unlocked_pack bikemesa_auto_pack)
