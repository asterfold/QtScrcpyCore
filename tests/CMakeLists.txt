cmake_minimum_required(VERSION 3.19)
project(touch_mapping_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_AUTOMOC ON)
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network)
set(SRC ../src)
set(CTRL ${SRC}/device/controller)
set(INPUT ${CTRL}/inputconvert)
add_executable(touch_mapping_tests
    touch_mapping_tests.cpp
    ${CTRL}/controller.cpp ${CTRL}/bufferutil.cpp
    ${CTRL}/receiver/receiver.cpp ${CTRL}/receiver/devicemsg.cpp
    ${INPUT}/inputconvertbase.cpp ${INPUT}/inputconvertnormal.cpp
    ${INPUT}/inputconvertgame.cpp ${INPUT}/controlmsg.cpp ${INPUT}/keymap/keymap.cpp)
target_include_directories(touch_mapping_tests PRIVATE
    ${SRC}/common ${SRC}/device/android ${SRC}/device/server
    ${CTRL} ${CTRL}/receiver ${INPUT} ${INPUT}/keymap)
target_link_libraries(touch_mapping_tests PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network)
if(APPLE)
    target_link_libraries(touch_mapping_tests PRIVATE "-framework CoreGraphics")
endif()
enable_testing()
add_test(NAME touch_mapping_tests COMMAND touch_mapping_tests)
set_tests_properties(touch_mapping_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set(TOUCH_PROFILE "" CACHE FILEPATH "Optional real keymap profile to exercise")
if(TOUCH_PROFILE)
    add_test(NAME touch_profile_tests COMMAND touch_mapping_tests "${TOUCH_PROFILE}")
    set_tests_properties(touch_profile_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
