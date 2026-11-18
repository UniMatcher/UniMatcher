if(TARGET UniMatcher::core)
  return()
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
   NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
  message(FATAL_ERROR "The packaged UniMatcher library requires Linux x86-64.")
endif()

get_filename_component(_unimatcher_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
add_library(UniMatcher::core SHARED IMPORTED GLOBAL)
set_target_properties(UniMatcher::core PROPERTIES
  IMPORTED_LOCATION "${_unimatcher_root}/lib/linux-x86_64/libunimatcher.so"
  INTERFACE_INCLUDE_DIRECTORIES "${_unimatcher_root}/include"
  INTERFACE_COMPILE_FEATURES cxx_std_17)
unset(_unimatcher_root)
