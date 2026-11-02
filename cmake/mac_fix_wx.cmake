# macOS POST_BUILD step: point every libwx_* load command of CG_DYLIB at OpenCPN.app's short framework name.
# Run as: cmake -DCG_DYLIB=<file> -P mac_fix_wx.cmake
execute_process(COMMAND otool -L "${CG_DYLIB}" OUTPUT_VARIABLE CG_OTOOL RESULT_VARIABLE CG_RC)
if(NOT CG_RC EQUAL 0)
    message(FATAL_ERROR "otool -L failed on ${CG_DYLIB}")
endif()
string(REGEX MATCHALL "[^ \t\n]*/libwx_[A-Za-z0-9_]+-3\\.2[0-9.]*\\.dylib" CG_WX_REFS "${CG_OTOOL}")
list(REMOVE_DUPLICATES CG_WX_REFS) # a universal file lists each path once per architecture
foreach(CG_OLD IN LISTS CG_WX_REFS)
    string(REGEX REPLACE "^.*/(libwx_[A-Za-z0-9_]+)-3\\.2[0-9.]*\\.dylib$" "\\1" CG_BASE "${CG_OLD}")
    execute_process(COMMAND install_name_tool -change "${CG_OLD}"
        "@executable_path/../Frameworks/${CG_BASE}-3.2.dylib" "${CG_DYLIB}" RESULT_VARIABLE CG_RC)
    if(NOT CG_RC EQUAL 0)
        message(FATAL_ERROR "install_name_tool -change ${CG_OLD} failed")
    endif()
endforeach()
