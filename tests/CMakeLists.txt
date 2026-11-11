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
)
target_link_libraries(vcr_tests PRIVATE viceroy::core)
target_compile_definitions(vcr_tests PRIVATE VICEROY_GAMEDATA_DIR="${PROJECT_SOURCE_DIR}/gamedata")
vcr_set_warnings(vcr_tests)

add_test(NAME vcr_tests COMMAND vcr_tests)
