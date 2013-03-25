INCLUDE(FindPkgConfig)
PKG_CHECK_MODULES(PC_GNURADIO_BLOCKS gnuradio-blocks>=3.7)

FIND_PATH(
    GNURADIO_BLOCKS_INCLUDE_DIRS
    NAMES gnuradio/blocks/api.h
    HINTS $ENV{GNURADIO_BLOCKS_DIR}/include
        ${PC_GNURADIO_BLOCKS_INCLUDEDIR}
    PATHS /usr/local/include
          /usr/include
)

FIND_LIBRARY(
    GNURADIO_BLOCKS_LIBRARIES
    NAMES gnuradio-blocks
    HINTS $ENV{GNURADIO_BLOCKS_DIR}/lib
        ${PC_GNURADIO_BLOCKS_LIBDIR}
    PATHS /usr/local/lib
          /usr/local/lib64
          /usr/lib
          /usr/lib64
)

INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(GNURADIO_BLOCKS DEFAULT_MSG GNURADIO_BLOCKS_LIBRARIES GNURADIO_BLOCKS_INCLUDE_DIRS)
MARK_AS_ADVANCED(GNURADIO_BLOCKS_LIBRARIES GNURADIO_BLOCKS_INCLUDE_DIRS)