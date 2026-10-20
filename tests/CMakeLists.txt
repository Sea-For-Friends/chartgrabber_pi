# GoogleTest wiring. Append ONE test source per line at the END of the list.
set(CG_TEST_SOURCES
    cli_records_test.cpp
    geo_math_test.cpp
    job_files_test.cpp
    area_tool_test.cpp
    wx_store_test.cpp
    cli_runner_test.cpp
    wx_env.cpp
    messages_test.cpp
    ocpn_fakes.cpp
    test_guards_test.cpp
    cli_name_test.cpp
    cli_text_test.cpp
    plugin_version_test.cpp
    contract_test.cpp
    guard_test.cpp
    strip_place_test.cpp
    sources_folder_test.cpp
    toolbar_icons_test.cpp
    session_log_test.cpp
    job_progress_test.cpp
)

# Window tests (see the rule in wx_env.h): a second binary with a GUI wxApp, ctest label "gui".
set(CG_GUI_TEST_SOURCES
    progress_strip_test.cpp
    gui_env_test.cpp
    ocpn_fakes.cpp
    job_controller_test.cpp
    settings_dialog_test.cpp
    main_dialog_test.cpp
    new_chart_panel_test.cpp
    my_charts_panel_test.cpp
    sources_dialog_test.cpp
    look_test.cpp
    chartgrabber_pi_test.cpp
    question_test.cpp
    plugin_log_test.cpp
    close_during_job_test.cpp
)

if(CG_TEST_SOURCES)
    add_executable(cg_tests ${CG_TEST_SOURCES})
    # wx_store_test.cpp builds src/wx/config_store.cpp and look.cpp; area_tool_test.cpp builds area_tool.cpp;
    # cli_runner_test.cpp builds cli_runner.cpp.
    # The OpenCPN API and GL are faked in the tests.
    target_sources(cg_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src/wx/area_tool.cpp
        ${CMAKE_SOURCE_DIR}/src/wx/cli_runner.cpp
        ${CMAKE_SOURCE_DIR}/src/wx/config_store.cpp
        ${CMAKE_SOURCE_DIR}/src/wx/look.cpp
        ${CMAKE_SOURCE_DIR}/src/wx/messages.cpp
        ${CMAKE_SOURCE_DIR}/src/wx/plugin_log.cpp
    )
    target_include_directories(cg_tests SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/api ${wxWidgets_INCLUDE_DIRS})
    target_include_directories(cg_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/wx)
    target_compile_definitions(cg_tests PRIVATE ${wxWidgets_DEFINITIONS})
    target_compile_definitions(cg_tests PRIVATE CG_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
        CG_VERSION_MAJOR=${PROJECT_VERSION_MAJOR} CG_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        CG_VERSION_PATCH=${PROJECT_VERSION_PATCH} "CG_VERSION_STRING=\"${PROJECT_VERSION}\""
        CG_BUILD_DIR="${CMAKE_BINARY_DIR}" CG_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(cg_tests PRIVATE
        cg_core
        cg_options
        ${wxWidgets_LIBRARIES}
        GTest::gtest_main
    )
    include(GoogleTest)
    gtest_discover_tests(cg_tests DISCOVERY_TIMEOUT 60)
endif()

if(CG_GUI_TEST_SOURCES)
    # settings_dialog_test.cpp dismisses wxGTK's native message box through GTK (wx 3.2 is built on GTK 3).
    add_executable(cg_gui_tests ${CG_GUI_TEST_SOURCES} wx_env.cpp)
    target_sources(cg_gui_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src/wx/look.cpp
        ${CMAKE_SOURCE_DIR}/src/wx/messages.cpp
        ${CMAKE_SOURCE_DIR}/src/wx/progress_strip.cpp
        ${CMAKE_SOURCE_DIR}/src/wx/area_tool.cpp
        ${CMAKE_SOURCE_DIR}/src/wx/cli_runner.cpp
        ${CMAKE_SOURCE_DIR}/src/wx/config_store.cpp
        ${CMAKE_SOURCE_DIR}/src/wx/job_controller.cpp
        ${CMAKE_SOURCE_DIR}/src/wx/settings_dialog.cpp
        ${CMAKE_SOURCE_DIR}/src/wx/main_dialog.cpp
        ${CMAKE_SOURCE_DIR}/src/wx/new_chart_panel.cpp
        ${CMAKE_SOURCE_DIR}/src/wx/my_charts_panel.cpp
        ${CMAKE_SOURCE_DIR}/src/wx/sources_dialog.cpp
        ${CMAKE_SOURCE_DIR}/src/wx/chartgrabber_pi.cpp
        ${CMAKE_SOURCE_DIR}/src/wx/question.cpp
        ${CMAKE_SOURCE_DIR}/src/wx/plugin_log.cpp
    )
    target_include_directories(cg_gui_tests SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/api ${wxWidgets_INCLUDE_DIRS})
    target_include_directories(cg_gui_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/wx)
    target_compile_definitions(cg_gui_tests PRIVATE ${wxWidgets_DEFINITIONS} CG_WX_GUI_TESTS
        CG_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
        CG_VERSION_MAJOR=${PROJECT_VERSION_MAJOR} CG_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        CG_VERSION_PATCH=${PROJECT_VERSION_PATCH} "CG_VERSION_STRING=\"${PROJECT_VERSION}\"")
    target_link_libraries(cg_gui_tests PRIVATE cg_core cg_options ${wxWidgets_LIBRARIES} OpenGL::GL PkgConfig::GTK3 GTest::gtest_main)
    include(GoogleTest)
    # Every window test runs on its own private Xvfb through tools/xvfb_test.sh, so ctest -j runs them side by side;
    # label "gui" selects them (ctest -L gui). Discovery goes through it too. TEST_LAUNCHER needs CMake 3.29;
    # older CMake (Debian 12: 3.25) gets the same launcher through CROSSCOMPILING_EMULATOR (command = launcher + binary).
    if(CMAKE_VERSION VERSION_LESS 3.29)
        set_property(TARGET cg_gui_tests PROPERTY CROSSCOMPILING_EMULATOR ${CMAKE_SOURCE_DIR}/tools/xvfb_test.sh)
    else()
        set_property(TARGET cg_gui_tests PROPERTY TEST_LAUNCHER ${CMAKE_SOURCE_DIR}/tools/xvfb_test.sh)
    endif()
    gtest_discover_tests(cg_gui_tests DISCOVERY_TIMEOUT 60
        PROPERTIES LABELS gui)
endif()
