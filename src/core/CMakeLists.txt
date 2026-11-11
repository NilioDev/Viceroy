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
    src/game/colony.cpp
    src/render/screen.cpp
    src/render/game_view.cpp
    src/render/colony_view.cpp
    src/ui/play.cpp
)
add_library(viceroy::core ALIAS vcr_core)

target_include_directories(vcr_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_features(vcr_core PUBLIC cxx_std_20)
set_target_properties(vcr_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
vcr_set_warnings(vcr_core)
