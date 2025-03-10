cmake_minimum_required(VERSION 3.10)
project(OpenMarketSimEngine)

set(CMAKE_CXX_STANDARD 17)

# Find and link ZeroMQ
find_package(ZeroMQ REQUIRED)
include_directories(${ZeroMQ_INCLUDE_DIRS})

add_executable(engine src/main.cpp)
target_link_libraries(engine ${ZeroMQ_LIBRARIES})
