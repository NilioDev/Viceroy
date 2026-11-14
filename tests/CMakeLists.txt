add_executable(vcr_tests
    test_main.cpp
    test_game_files.cpp
    test_formats.cpp
    test_text_and_fonts.cpp
    test_ruleset.cpp
    test_savegame.cpp
    test_import.cpp
    test_game.cpp
    test_colony.cpp
    test_europe.cpp
    test_resource.cpp
    test_new_game.cpp
    test_pioneers.cpp
    test_crosses.cpp
    test_liberty.cpp
    test_colony_reports.cpp
    test_combat.cpp
    test_natives.cpp
    test_euro_ai.cpp
    test_congress.cpp
    test_king.cpp
    test_rumours.cpp
    test_routes.cpp
    test_menu.cpp
    test_title.cpp
    test_sound.cpp
)
target_link_libraries(vcr_tests PRIVATE viceroy::core)
target_compile_definitions(vcr_tests PRIVATE VICEROY_GAMEDATA_DIR="${PROJECT_SOURCE_DIR}/gamedata")
vcr_set_warnings(vcr_tests)

add_test(NAME vcr_tests COMMAND vcr_tests)
