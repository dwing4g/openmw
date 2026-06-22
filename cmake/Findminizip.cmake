# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
Findminizip
-----------

Find the minizip include directory and library.

Use this module by invoking find_package with the form::

.. code-block:: cmake

  find_package(minizip
    [REQUIRED]             # Fail with error if minizip is not found
  )

Imported targets
^^^^^^^^^^^^^^^^

This module defines the following :prop_tgt:`IMPORTED` targets:

.. variable:: minizip::minizip

  Imported target for using the minizip library, if found.

Result variables
^^^^^^^^^^^^^^^^

.. variable:: minizip_FOUND

  Set to true if minizip library found, otherwise false or undefined.

.. variable:: minizip_INCLUDE_DIRS

  Paths to include directories listed in one variable for use by minizip client.

.. variable:: minizip_LIBRARIES

  Paths to libraries to linked against to use minizip.

Cache variables
^^^^^^^^^^^^^^^

For users who wish to edit and control the module behavior, this module
reads hints about search locations from the following variables::

.. variable:: minizip_INCLUDE_DIR

  Path to minizip include directory with ``minizip/unzip.h`` header.

.. variable:: minizip_LIBRARY

  Path to minizip library to be linked.

NOTE: The variables above should not usually be used in CMakeLists.txt files!

#]=======================================================================]

### Find library ##############################################################

if(NOT minizip_LIBRARY)
    find_library(minizip_LIBRARY_RELEASE NAMES minizip)
    find_library(minizip_LIBRARY_DEBUG NAMES minizipd)

    include(SelectLibraryConfigurations)
    select_library_configurations(minizip)
else()
    file(TO_CMAKE_PATH "${minizip_LIBRARY}" minizip_LIBRARY)
endif()

### Find include directory ####################################################
find_path(minizip_INCLUDE_DIR NAMES minizip/unzip.h)

### Set result variables ######################################################
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(minizip DEFAULT_MSG
        minizip_LIBRARY minizip_INCLUDE_DIR)

set(minizip_INCLUDE_DIR ${minizip_INCLUDE_DIR} CACHE PATH "minizip include dir hint")
set(minizip_LIBRARY ${minizip_LIBRARY} CACHE FILEPATH "minizip library path hint")
mark_as_advanced(minizip_INCLUDE_DIR minizip_LIBRARY)

set(minizip_LIBRARIES ${minizip_LIBRARY})
set(minizip_INCLUDE_DIRS ${minizip_INCLUDE_DIR})

### Import targets ############################################################
if(minizip_FOUND)
    if(NOT TARGET minizip::minizip)
        add_library(minizip::minizip UNKNOWN IMPORTED)
        set_target_properties(minizip::minizip PROPERTIES
                IMPORTED_LINK_INTERFACE_LANGUAGES "C"
                INTERFACE_INCLUDE_DIRECTORIES "${minizip_INCLUDE_DIR}")

        if(minizip_LIBRARY_RELEASE)
            set_property(TARGET minizip::minizip APPEND PROPERTY
                    IMPORTED_CONFIGURATIONS RELEASE)
            set_target_properties(minizip::minizip PROPERTIES
                    IMPORTED_LOCATION_RELEASE "${minizip_LIBRARY_RELEASE}")
        endif()

        if(minizip_LIBRARY_DEBUG)
            set_property(TARGET minizip::minizip APPEND PROPERTY
                    IMPORTED_CONFIGURATIONS DEBUG)
            set_target_properties(minizip::minizip PROPERTIES
                    IMPORTED_LOCATION_DEBUG "${minizip_LIBRARY_DEBUG}")
        endif()

        if(NOT minizip_LIBRARY_RELEASE AND NOT minizip_LIBRARY_DEBUG)
            set_property(TARGET minizip::minizip APPEND PROPERTY
                    IMPORTED_LOCATION "${minizip_LIBRARY}")
        endif()
    endif()
endif()