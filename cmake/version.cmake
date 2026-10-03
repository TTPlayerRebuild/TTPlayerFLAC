set(TTP_FLAC_BUILD_VERSION "" CACHE STRING "Beijing yyyy.MM.dd[pN] version; empty selects the current date")
find_program(TTP_FLAC_POWERSHELL NAMES powershell pwsh REQUIRED)
set(_flac_version_args)
if(NOT TTP_FLAC_BUILD_VERSION STREQUAL "")
  list(APPEND _flac_version_args -Version "${TTP_FLAC_BUILD_VERSION}")
endif()
add_custom_target(ttp_flac_version
  COMMAND "${TTP_FLAC_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
    -File "${CMAKE_CURRENT_SOURCE_DIR}/cmake/write_version.ps1"
    -Template "${CMAKE_CURRENT_SOURCE_DIR}/src/version.rc.in"
    -OutputPath "${CMAKE_CURRENT_BINARY_DIR}/generated/version.rc"
    ${_flac_version_args}
  BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/generated/version.rc"
  COMMENT "Updating FLAC file and product versions"
  VERBATIM)
