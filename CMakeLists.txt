cmake_minimum_required(VERSION 3.10)

project(Cache CXX)

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -Wall -g")

add_executable(main test.cpp)
