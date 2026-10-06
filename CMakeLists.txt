cmake_minimum_required(VERSION 3.24)
project(TtpFlac LANGUAGES C CXX RC)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "ttp_flac requires MSVC and -A Win32")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(cmake/legacy_windows.cmake)
include(cmake/size_release.cmake)
include(cmake/flac.cmake)
include(cmake/version.cmake)
add_library(ttp_flac SHARED src/plugin.cpp src/reader.cpp src/metadata.cpp src/external_tags.cpp src/tta.cpp
  src/ttp_flac.def "${CMAKE_CURRENT_BINARY_DIR}/generated/version.rc")
add_dependencies(ttp_flac ttp_flac_version)
target_include_directories(ttp_flac PUBLIC include PRIVATE src)
target_compile_definitions(ttp_flac PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
  _WIN32_WINNT=0x0501 _CRT_SECURE_NO_WARNINGS)
target_compile_options(ttp_flac PRIVATE /W4 /permissive- /EHsc /GR- /Os /Gy /Gw /GF /utf-8 /fp:precise /arch:SSE2)
target_link_libraries(ttp_flac PRIVATE FLAC ole32 uuid shlwapi)
target_link_options(ttp_flac PRIVATE /OPT:REF /OPT:ICF /INCREMENTAL:NO /DEBUG:NONE)
set_target_properties(ttp_flac PROPERTIES PREFIX "" OUTPUT_NAME "ttp_flac")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(TARGET ttp_flac POST_BUILD COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_legacy_imports.py" "$<TARGET_FILE:ttp_flac>"
  --exports "${ttplayer_yy_thunks_SOURCE_DIR}/Config/x86/5.1.2600.txt"
  --exports "${ttplayer_yy_thunks_SOURCE_DIR}/Config/x86/6.1.7600.txt"
  --report "$<TARGET_FILE_DIR:ttp_flac>/legacy-imports.json" VERBATIM)
install(TARGETS ttp_flac RUNTIME DESTINATION AddIn COMPONENT Runtime)
