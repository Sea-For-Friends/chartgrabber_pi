# Static library cg_core: C++23, std only, no wx, no user-visible text (namespace cg).
# Append ONE source file per line at the END of this list.
set(CG_CORE_SOURCES
    cli_records.cpp
geo_math.cpp
    job_files.cpp
    cli_text.cpp
    strip_place.cpp
    sources_folder.cpp
    session_log.cpp
    job_progress.cpp
)

if(CG_CORE_SOURCES)
    add_library(cg_core STATIC ${CG_CORE_SOURCES})
    target_link_libraries(cg_core PRIVATE cg_options)
    set(CG_CORE_SCOPE PUBLIC)
else()
    # No sources yet (types.h / settings.h are header-only): an interface library keeps the target name.
    add_library(cg_core INTERFACE)
    set(CG_CORE_SCOPE INTERFACE)
endif()
target_include_directories(cg_core ${CG_CORE_SCOPE} ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(cg_core ${CG_CORE_SCOPE} cxx_std_23)
