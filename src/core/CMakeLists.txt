configure_file(version.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/viceroy/version.hpp" @ONLY)

add_library(vcr_core STATIC
    src/game_files.cpp
    src/data/madspack.cpp
    src/data/image.cpp
    src/data/font.cpp
    src/data/text_table.cpp
    src/rules/ruleset.cpp
    src/data/savegame.cpp
    src/util/zlib.cpp
    src/util/zip.cpp
    src/pack/library.cpp
    src/game/game.cpp
    src/game/combat.cpp
    src/game/congress.cpp
    src/game/natives.cpp
    src/game/native_trade.cpp
    src/game/native_tribute.cpp
    src/game/euro_ai.cpp
    src/game/euro_ai_units.cpp
    src/game/euro_ai_colony.cpp
    src/game/native_attack.cpp
    src/game/native_contact.cpp
    src/game/native_incite.cpp
    src/game/native_heresy.cpp
    src/game/native_move.cpp
    src/game/native_raid.cpp
    src/game/native_visit.cpp
    src/game/rumour.cpp
    src/game/route.cpp
    src/game/goto.cpp
    src/game/native_turn.cpp
    src/game/colony.cpp
    src/game/europe.cpp
    src/game/resource.cpp
    src/game/new_game.cpp
    src/render/screen.cpp
    src/render/game_view.cpp
    src/render/colony_view.cpp
    src/render/europe_view.cpp
    src/render/dialog.cpp
    src/render/menu_bar.cpp
    src/render/report_view.cpp
    src/ui/play.cpp
    src/ui/title.cpp
    src/ui/new_game_flow.cpp
    src/ui/debug_menu.cpp
    src/sound/background_music.cpp
    src/sound/music_driver.cpp
    src/sound/music_stream.cpp
)
add_library(viceroy::core ALIAS vcr_core)

# Nuked OPL3 (LGPL 2.1+), the FM chip emulator. Third-party code: built
# without our warning settings.
add_library(vcr_opl3 STATIC "${PROJECT_SOURCE_DIR}/third_party/nuked-opl3/opl3.c")
target_include_directories(vcr_opl3 PUBLIC "${PROJECT_SOURCE_DIR}/third_party/nuked-opl3")
set_target_properties(vcr_opl3 PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(vcr_core PRIVATE vcr_opl3)

target_include_directories(vcr_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_features(vcr_core PUBLIC cxx_std_20)
set_target_properties(vcr_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
vcr_set_warnings(vcr_core)
