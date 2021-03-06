# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2010-2019 Intel Corporation
cmake_minimum_required (VERSION 3.1)

add_library (LmsEventManager SHARED 
  eventManager.cpp
  $<$<BOOL:${WIN32}>:EventManager.rc>
)

if (WIN32)
  set_target_properties(LmsEventManager PROPERTIES OUTPUT_NAME EventManager)
endif (WIN32)

set_target_properties (LmsEventManager PROPERTIES VERSION ${LMS_VERSION_STRING})
set_target_properties (LmsEventManager PROPERTIES SOVERSION ${LMS_VERSION_STRING})
set_target_properties (LmsEventManager PROPERTIES CXX_VISIBILITY_PRESET hidden)

target_link_libraries (LmsEventManager
  LmsGMS_COMMON
  ACELIB
)

target_compile_definitions (LmsEventManager PRIVATE
  EVENTMANAGER_BUILD_DLL
)

install (TARGETS LmsEventManager
  LIBRARY DESTINATION ${LMS_LIBDIR}
  RUNTIME DESTINATION ${LMS_LIBDIR}
)
if (CMAKE_CXX_COMPILER_ID MATCHES MSVC)
  install (FILES $<TARGET_PDB_FILE:LmsEventManager>
    DESTINATION ${LMS_LIBDIR_DBG} OPTIONAL
  )
endif (CMAKE_CXX_COMPILER_ID MATCHES MSVC)
