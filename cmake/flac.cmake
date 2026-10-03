include(FetchContent)
FetchContent_Declare(ttplayer_flac
  URL https://downloads.xiph.org/releases/flac/flac-1.5.0.tar.xz
  URL_HASH SHA256=f2c1c76592a82ffff8413ba3c4a1299b6c7ab06c734dee03fd88630485c2b920
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
foreach(option BUILD_SHARED_LIBS BUILD_CXXLIBS BUILD_PROGRAMS BUILD_EXAMPLES BUILD_TESTING BUILD_DOCS
    INSTALL_MANPAGES INSTALL_PKGCONFIG_MODULES INSTALL_CMAKE_CONFIG_MODULE WITH_OGG ENABLE_MULTITHREADING WITH_AVX)
  set(${option} OFF CACHE BOOL "Independent FLAC input plugin" FORCE)
endforeach()
FetchContent_MakeAvailable(ttplayer_flac)
target_compile_options(FLAC PRIVATE /O1 /Os /Gy /Gw /GF /arch:SSE2 /utf-8 /wd4244 /wd4267 /wd4996)
