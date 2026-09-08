cmake_minimum_required(VERSION 3.20)

project(
    virtual_light_booth
    VERSION 1.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


# -----------------------------------------------------------------------------
# Build configuration
# -----------------------------------------------------------------------------

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(
        CMAKE_BUILD_TYPE
        RelWithDebInfo
        CACHE STRING
        "Choose the type of build."
        FORCE
    )
endif()


# -----------------------------------------------------------------------------
# Dependencies
# -----------------------------------------------------------------------------

find_package(OpenCV REQUIRED COMPONENTS core highgui imgcodecs imgproc)

include(FetchContent)

FetchContent_Declare(
  portable_file_dialogs
  GIT_REPOSITORY https://github.com/samhocevar/portable-file-dialogs.git
  GIT_TAG        c12ea8c
  SOURCE_SUBDIR  _do_not_build
)
FetchContent_MakeAvailable(portable_file_dialogs)

add_library(portable_file_dialogs INTERFACE)

target_include_directories(
  portable_file_dialogs
  INTERFACE
    ${portable_file_dialogs_SOURCE_DIR}
)


# -----------------------------------------------------------------------------
# Load camera models
# -----------------------------------------------------------------------------

# Assemble the runtime data directory in the build tree. The Jiang camera
# database is kept in its original text format. If it is not already present
# in source data/, CMake tries the current Zenodo REST content endpoint first
# and the original project-hosted copy second.

set(VLB_BUILD_DATA_DIR "${CMAKE_CURRENT_BINARY_DIR}/data")
file(MAKE_DIRECTORY "${VLB_BUILD_DATA_DIR}")
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/data/"
     DESTINATION "${VLB_BUILD_DATA_DIR}")

set(JIANG_CAMERA_DATABASE "${VLB_BUILD_DATA_DIR}/camspec_database.txt")
set(JIANG_CAMERA_DATABASE_MD5 "dcf7ce9ec9f2c87b87014f5cc46f8c15")

if(EXISTS "${JIANG_CAMERA_DATABASE}")
  file(MD5 "${JIANG_CAMERA_DATABASE}" JIANG_EXISTING_MD5)
  if(NOT JIANG_EXISTING_MD5 STREQUAL JIANG_CAMERA_DATABASE_MD5)
    message(STATUS
      "Existing Jiang camera database failed checksum; removing it"
    )
    file(REMOVE "${JIANG_CAMERA_DATABASE}")
  endif()
endif()

if(NOT EXISTS "${JIANG_CAMERA_DATABASE}")
  set(JIANG_CAMERA_DATABASE_URLS
    "https://zenodo.org/api/records/3245883/files/camspec_database.txt/content"
    "https://www.gujinwei.org/research/camspec/camspec_database.txt"
  )

  set(JIANG_DOWNLOAD_SUCCEEDED FALSE)
  foreach(JIANG_DOWNLOAD_URL IN LISTS JIANG_CAMERA_DATABASE_URLS)
    message(STATUS
      "Downloading Jiang et al. camera database from ${JIANG_DOWNLOAD_URL}"
    )

    # Do not use EXPECTED_HASH here. If the HTTP request itself fails, recent
    # CMake versions attempt to hash the failed download and emit a less useful
    # secondary error. Verify the checksum explicitly after a successful HTTP
    # transfer instead.
    file(
      DOWNLOAD
        "${JIANG_DOWNLOAD_URL}"
        "${JIANG_CAMERA_DATABASE}"
      TLS_VERIFY ON
      HTTPHEADER "User-Agent: virtual-light-booth/1.1"
      STATUS JIANG_DOWNLOAD_STATUS
    )

    list(GET JIANG_DOWNLOAD_STATUS 0 JIANG_DOWNLOAD_CODE)
    list(GET JIANG_DOWNLOAD_STATUS 1 JIANG_DOWNLOAD_MESSAGE)

    if(JIANG_DOWNLOAD_CODE EQUAL 0)
      file(MD5 "${JIANG_CAMERA_DATABASE}" JIANG_DOWNLOADED_MD5)
      if(JIANG_DOWNLOADED_MD5 STREQUAL JIANG_CAMERA_DATABASE_MD5)
        set(JIANG_DOWNLOAD_SUCCEEDED TRUE)
        break()
      endif()

      message(STATUS
        "Downloaded Jiang database failed checksum; trying another source"
      )
    else()
      message(STATUS
        "Jiang database download failed: ${JIANG_DOWNLOAD_MESSAGE}"
      )
    endif()

    file(REMOVE "${JIANG_CAMERA_DATABASE}")
  endforeach()

  if(NOT JIANG_DOWNLOAD_SUCCEEDED)
    message(FATAL_ERROR
      "Unable to obtain the Jiang camera spectral-sensitivity database.\n"
      "Place the original camspec_database.txt in the project's data/ "
      "directory and configure again."
    )
  endif()
endif()


# -----------------------------------------------------------------------------
# Applications
# -----------------------------------------------------------------------------

add_executable(virtual_light_booth
  apps/virtual_light_booth.cpp
  src/SpectralData.cpp
  src/ColorScience.cpp
)

target_include_directories(
  virtual_light_booth
  PRIVATE
    include
)

target_link_libraries(
  virtual_light_booth
  PRIVATE
    ${OpenCV_LIBS}
    portable_file_dialogs
)

target_compile_definitions(
  virtual_light_booth
  PRIVATE
    VLB_BUILD_DATA_DIR="${VLB_BUILD_DATA_DIR}"
    VLB_SOURCE_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
)


# -----------------------------------------------------------------------------
# Project-wide target settings
# -----------------------------------------------------------------------------

if(MSVC)
  target_compile_options(virtual_light_booth PRIVATE /W4)
else()
  target_compile_options(virtual_light_booth PRIVATE -Wall -Wextra -Wpedantic)
endif()
