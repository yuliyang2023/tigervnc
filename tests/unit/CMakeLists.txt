if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  message(FATAL_ERROR "cmake must be invoked with the top level directory")
endif()

include(GoogleTest)

# Google Test requires a new C++ standard
if(MSVC)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++17")
else()
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++17")
endif()

enable_testing()

include_directories(${CMAKE_SOURCE_DIR}/vncviewer)

add_executable(configargs configargs.cxx)
target_link_libraries(configargs rfb GTest::gtest_main)
gtest_discover_tests(configargs)

add_executable(conv conv.cxx)
target_link_libraries(conv rfb GTest::gtest_main)
gtest_discover_tests(conv)

add_executable(convertlf convertlf.cxx)
target_link_libraries(convertlf core GTest::gtest_main)
gtest_discover_tests(convertlf)

add_executable(gesturehandler gesturehandler.cxx ../../vncviewer/GestureHandler.cxx)
target_link_libraries(gesturehandler core GTest::gtest_main)
gtest_discover_tests(gesturehandler)

add_executable(hostport hostport.cxx)
target_link_libraries(hostport network GTest::gtest_main)
gtest_discover_tests(hostport)

add_executable(parameters parameters.cxx)
target_link_libraries(parameters core GTest::gtest_main)
gtest_discover_tests(parameters)

add_executable(pixelformat pixelformat.cxx)
target_link_libraries(pixelformat rfb GTest::gtest_main)
gtest_discover_tests(pixelformat)

add_executable(shortcuthandler shortcuthandler.cxx ../../vncviewer/ShortcutHandler.cxx)
target_link_libraries(shortcuthandler core ${Intl_LIBRARIES} GTest::gtest_main)
gtest_discover_tests(shortcuthandler)

add_executable(unicode unicode.cxx)
target_link_libraries(unicode core GTest::gtest_main)
gtest_discover_tests(unicode)

add_executable(emulatemb emulatemb.cxx ../../vncviewer/EmulateMB.cxx)
target_link_libraries(emulatemb core GTest::gtest_main)
gtest_discover_tests(emulatemb)

if(NOT WIN32)
  find_package(Threads REQUIRED)
  add_executable(socks5 socks5.cxx)
  target_link_libraries(socks5 network GTest::gtest_main Threads::Threads)
  gtest_discover_tests(socks5 PROPERTIES TIMEOUT 15)
endif()

add_executable(jpeg jpeg.cxx)
target_link_libraries(jpeg rfb GTest::gtest_main)
if(FLTK_FOUND)
  target_compile_definitions(jpeg PRIVATE TEST_FLTK_JPEG)
  target_include_directories(jpeg SYSTEM PRIVATE ${FLTK_INCLUDE_DIR})
  target_link_libraries(jpeg ${FLTK_LIBRARIES})
endif()
gtest_discover_tests(jpeg)
