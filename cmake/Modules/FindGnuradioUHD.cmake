INCLUDE(FindPkgConfig)
PKG_CHECK_MODULES(PC_GNURADIO_UHD gnuradio-uhd>=3.7)

FIND_PATH(
    GNURADIO_UHD_INCLUDE_DIRS
    NAMES gnuradio/gr_uhd/api.h
    HINTS $ENV{GNURADIO_UHD_DIR}/include
        ${PC_GNURADIO_UHD_INCLUDEDIR}
    PATHS /usr/local/include
          /usr/include
)

FIND_LIBRARY(
    GNURADIO_UHD_LIBRARIES
    NAMES gnuradio-uhd
    HINTS $ENV{GNURADIO_UHD_DIR}/lib
        ${PC_GNURADIO_UHD_LIBDIR}
    PATHS /usr/local/lib
          /usr/local/lib64
          /usr/lib
          /usr/lib64
)

if(GNURADIO_UHD_INCLUDE_DIRS AND GNURADIO_UHD_LIBRARIES)
  set(GNURADIO_UHD_FOUND TRUE CACHE INTERNAL "gnuradio-uhd found")
  message(STATUS "Found gnuradio-uhd: ${GNURADIO_UHD_INCLUDE_DIRS}, ${GNURADIO_UHD_LIBRARIES}")
else(GNURADIO_UHD_INCLUDE_DIRS AND GNURADIO_UHD_LIBRARIES)
  set(GNURADIO_UHD_FOUND FALSE CACHE INTERNAL "gnuradio-uhd found")
  message(STATUS "gnuradio-uhd not found.")
endif(GNURADIO_UHD_INCLUDE_DIRS AND GNURADIO_UHD_LIBRARIES)

INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(GNURADIO_UHD DEFAULT_MSG GNURADIO_UHD_LIBRARIES GNURADIO_UHD_INCLUDE_DIRS)
MARK_AS_ADVANCED(GNURADIO_UHD_LIBRARIES GNURADIO_UHD_INCLUDE_DIRS)
