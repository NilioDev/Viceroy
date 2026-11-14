set(VCR_APP_SOURCES
    main.cpp
    audio.cpp
)

if(ANDROID)
    # SDLActivity loads the game as libmain.so.
    add_library(vcr_app SHARED ${VCR_APP_SOURCES})
    set_target_properties(vcr_app PROPERTIES OUTPUT_NAME main)
else()
    add_executable(vcr_app WIN32 MACOSX_BUNDLE ${VCR_APP_SOURCES})
    set_target_properties(vcr_app PROPERTIES
        OUTPUT_NAME Viceroy
        MACOSX_BUNDLE_GUI_IDENTIFIER io.github.niliodev.viceroy
        MACOSX_BUNDLE_BUNDLE_NAME Viceroy
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
endif()

target_link_libraries(vcr_app PRIVATE viceroy::core SDL3::SDL3)
vcr_set_warnings(vcr_app)
