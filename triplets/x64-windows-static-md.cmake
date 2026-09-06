set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

# Overlay of the community x64-windows-static-md triplet.
#
# Why this exists: when more than one MSVC toolset version is installed
# side by side (common after in-place VS updates), vcpkg's own compiler
# detection picks the newest one on disk, while a .vcxproj pinned to
# <PlatformToolset>v143</PlatformToolset> without an explicit
# <VCToolsVersion> resolves to whatever Microsoft.VCToolsVersion.v143.default.txt
# says -- which can be an older toolset. Building a vcpkg port against a
# newer toolset than the one the solution actually links with can produce
# object code that calls compiler-internal STL symbols (e.g. vectorized
# find helpers) that the older toolset's import libs don't export yet,
# causing LNK2019 at the final .exe link step even though everything
# compiles cleanly.
#
# Directory.Build.props/.targets pass the solution's actual resolved
# $(VCToolsVersion) into the TOSKA_VCTOOLSVERSION environment variable
# before vcpkg runs, so vcpkg is forced to build with the exact same
# toolset the project itself links against, on any machine, regardless
# of how many toolsets happen to be installed or which one is "latest".
if(DEFINED ENV{TOSKA_VCTOOLSVERSION} AND NOT "$ENV{TOSKA_VCTOOLSVERSION}" STREQUAL "")
    set(VCPKG_PLATFORM_TOOLSET_VERSION "$ENV{TOSKA_VCTOOLSVERSION}")
endif()
set(VCPKG_ENV_PASSTHROUGH_UNTRACKED TOSKA_VCTOOLSVERSION)
