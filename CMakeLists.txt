# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

if(DEFINED ENV{RMAKER_PATH})
  set(RMAKER_PATH $ENV{RMAKER_PATH})
else()
  set(RMAKER_PATH ""C:/Users/leesh/PROJECT_AES/esp-rainmaker"") # adjust to your actual esp-rainmaker path
endif(DEFINED ENV{RMAKER_PATH})

# Add RainMaker components and other common application components
set(EXTRA_COMPONENT_DIRS
    ${RMAKER_PATH}/components/esp_rainmaker
    ${RMAKER_PATH}/examples/common
)


# Make the DHT11 header available to all targets
if(EXISTS "${CMAKE_SOURCE_DIR}/components/esp32-DHT11-master/include/dht11.h")
  include_directories("${CMAKE_SOURCE_DIR}/components/esp32-DHT11-master/include")
endif()

# Make the DHT11 header available to all targets
if(EXISTS "${CMAKE_SOURCE_DIR}/components/esp32-DHT11-master/")
  include_directories("${CMAKE_SOURCE_DIR}/components/esp32-DHT11-master/")
endif()


# Add local project components only if the directory exists
if(EXISTS "C:/Users/leesh/esp/v5.5.1/esp-idf/components/")
  list(APPEND EXTRA_COMPONENT_DIRS "C:/Users/leesh/esp/v5.5.1/esp-idf/components/")
endif()

set(PROJECT_VER "1.4")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(SmartHomeSystem)
