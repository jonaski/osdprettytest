set(SOURCES
  mainwindow.cpp
  osdpretty.cpp
)
set(HEADERS
  mainwindow.h
  osdpretty.h
)

set(UI mainwindow.ui osdpretty.ui)
set(RESOURCES ../data/data.qrc)

if(Qt6Core_FOUND)
  qt6_wrap_cpp(MOC ${HEADERS})
  qt6_wrap_ui(UIC ${UI})
  qt6_add_resources(QRC ${RESOURCES})
else()
  qt5_wrap_cpp(MOC ${HEADERS})
  qt5_wrap_ui(UIC ${UI})
  qt5_add_resources(QRC ${RESOURCES})
endif()

add_library(osdprettytest_lib STATIC
  ${SOURCES}
  ${MOC}
  ${UIC}
  ${QRC}
  ${POT}
  ${PO}
  ${OTHER_UIC_SOURCES}
)

target_include_directories(osdprettytest_lib PUBLIC
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_BINARY_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(osdprettytest_lib PUBLIC
  ${QT_LIBRARIES}
)

if(WIN32)
  target_link_libraries(osdprettytest_lib PUBLIC dwmapi)
endif()

if(MSVC)
  target_link_libraries(osdprettytest_lib PUBLIC legaosdprettytestcy_stdio_definitions)
endif()

set(EXECUTABLE_OUTPUT_PATH ..)

# Show the console window in debug mode on Windows
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT ENABLE_WIN32_CONSOLE)
  set(FILEINFO-WIN32-FLAG WIN32)
endif(NOT CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT ENABLE_WIN32_CONSOLE)

# Resource file for windows
if(WIN32)
  #configure_file(${CMAKE_CURRENT_SOURCE_DIR}/../dist/windows/windres.rc.in ${CMAKE_CURRENT_BINARY_DIR}/windres.rc)
  #set(FILEINFO-WIN32-RESOURCES windres.rc)
endif(WIN32)

add_executable(osdprettytest ${FILEINFO-WIN32-FLAG} ${FILEINFO-WIN32-RESOURCES} main.cpp)

target_link_libraries(osdprettytest PRIVATE osdprettytest_lib)

install(TARGETS osdprettytest RUNTIME DESTINATION bin)
