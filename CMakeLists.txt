cmake_minimum_required(VERSION 2.8)
project(hpinger)

set(hpinger_VERSION_MAJOR 1)
set(hpinger_VERSION_MINOR 0)

set(BOOST_COMPONENTS
	thread
	program_options)

include(FindOracle.cmake)
include(FindLibConfig.cmake)

find_library(Oracle REQUIRED)
find_package(Boost COMPONENTS ${BOOST_COMPONENTS} REQUIRED)
find_library(LibConfig REQUIRED)

include(FindOPing.cmake)
find_library(LibOPing REQUIRED)

include_directories(${ORACLE_INCLUDE_DIR} ${Boost_INCLUDE_DIRS} ${LIBCONFIG_INCLUDE_DIR} ${LIBOPING_INCLUDE_DIR})

add_executable(hpinger main.cpp)

target_link_libraries(hpinger ${ORACLE_LIBRARY} ${Boost_LIBRARIES} ${LIBCONFIG_LIBRARIES} ${LIBOPING_LIBRARY})
