cmake_minimum_required(VERSION 3.16)
project(ft8web LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW3F REQUIRED fftw3f)
pkg_check_modules(ALSA   REQUIRED alsa)
pkg_check_modules(HAMLIB REQUIRED hamlib)

set(CROW_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/crow/include)

add_executable(ft8web
  src/main.cpp
  src/RigClient.cpp
  src/AudioEngine.cpp
  src/DecodeEngine.cpp
  src/DecodeModes.cpp
  src/Waterfall.cpp
  src/FreqPlan.cpp
  src/Config.cpp
  src/PskReporter.cpp
  src/LogStore.cpp
  src/WorkedLog.cpp
  src/Uploader.cpp
  src/QrzLookup.cpp
  src/TxEngine.cpp
  src/GpsClient.cpp
  src/PotaClient.cpp
  src/QsoEngine.cpp
)

target_include_directories(ft8web PRIVATE
  ${CROW_INCLUDE_DIR}
  ${FFTW3F_INCLUDE_DIRS}
  ${ALSA_INCLUDE_DIRS}
  ${HAMLIB_INCLUDE_DIRS}
)

target_link_directories(ft8web PRIVATE ${HAMLIB_LIBRARY_DIRS})

target_link_libraries(ft8web PRIVATE
  Threads::Threads
  ${FFTW3F_LIBRARIES}
  ${ALSA_LIBRARIES}
  ${HAMLIB_LIBRARIES}
)

# jt9d: persistent jt9 shared-memory driver, keeps jt9's callsign hash table
# alive across decode periods. Needs Qt5Core for QSharedMemory (the same
# mechanism jt9 attaches with). ft8web stays Qt-free and falls back to
# per-slot jt9 when jt9d is absent.
find_package(Qt5 COMPONENTS Core QUIET)
if(Qt5Core_FOUND)
  add_executable(jt9d jt9d/jt9d.cpp)
  target_include_directories(jt9d PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/wsjtx-3.1.0/src/wsjtx)
  target_link_libraries(jt9d PRIVATE Qt5::Core)
else()
  message(WARNING "Qt5Core not found; jt9d not built, FT8 uses per-slot jt9")
endif()
