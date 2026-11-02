# Linux <target-arch> from the compiler, never from uname: an armhf chroot in an arm64 VM and a
# Pi with a 64-bit kernel report aarch64 to uname while the compiler is 32-bit (OpenCPN's cmake/GetArch.cmake does the same).
# In: CMAKE_SYSTEM_PROCESSOR, CMAKE_SIZEOF_VOID_P. Out: CG_HOST_MACHINE (x86_64, arm64 or armhf).
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" CG_PROC)
if(CG_PROC MATCHES "^(x86_64|amd64)$" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(CG_HOST_MACHINE "x86_64")
elseif(CG_PROC MATCHES "^(aarch64|arm64|arm.*)$" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(CG_HOST_MACHINE "arm64")
elseif(CG_PROC MATCHES "^(aarch64|arm64|arm.*)$" AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(CG_HOST_MACHINE "armhf")
else()
    message(FATAL_ERROR "unknown processor \"${CMAKE_SYSTEM_PROCESSOR}\" with pointer size ${CMAKE_SIZEOF_VOID_P}: "
        "pass -DCG_TARGET_ARCH")
endif()
