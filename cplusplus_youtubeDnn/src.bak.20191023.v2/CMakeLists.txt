# Copyright (c) 2018 by contributors.
# Authour: Chao Ma (mctt90@gmail.com)
#
# This is the root CMakeLists.txt file of the xLearn project, which
# build all subdirectories (packages) in the order of
# inter-package dependence.
#
# you can build the xLearn using the following commands:
#
# $> cd xLearn
# $> mkdir build
# $> cd build
# $> cmake ..
# $> make
#
# Thus you check out the xLearn project and build it in a
# subdirectory ``build". If you want further to install the
# built project, you can type the command:
#
# $> make install
#
project ("xLearn")

cmake_minimum_required(VERSION 2.8)

#-------------------------------------------------------------------------------
# Take almost all warnings;
# Do not generate debug symbols;
# Optimization level 3;
# Using c++11;
# Optimization on architecture
#-------------------------------------------------------------------------------
if(NOT WIN32)
add_definitions("-Wall -Wno-sign-compare -O3 -std=c++11
-march=native -Wno-strict-aliasing -Wno-comment")
#add_definitions("-Wall -Wno-sign-compare -O0 -std=c++11
#-march=native -Wno-strict-aliasing -Wno-comment")
else(WIN32)
add_definitions("/WX- /MT")
endif()

#-------------------------------------------------------------------------------
# Declare where our project will be installed.
#-------------------------------------------------------------------------------
set(CMAKE_INSTALL_PREFIX "xLearn")

#-------------------------------------------------------------------------------
# For Mac OSX
#-------------------------------------------------------------------------------
#set(CMAKE_MACOSX_RPATH TRUE)

#-------------------------------------------------------------------------------
# Set include paths and library paths.
#-------------------------------------------------------------------------------
include_directories(
  "${PROJECT_SOURCE_DIR}"
  "${PROJECT_BINARY_DIR}"
)

link_directories(
  "${PROJECT_BINARY_DIR}/src/base"
  "${PROJECT_BINARY_DIR}/src/data"
  "${PROJECT_BINARY_DIR}/src/reader"
  "${PROJECT_BINARY_DIR}/src/score"
  "${PROJECT_BINARY_DIR}/src/loss"
  "${PROJECT_BINARY_DIR}/src/solver"
)

#-------------------------------------------------------------------------------
# Declare packages in xLearn project.
#-------------------------------------------------------------------------------
add_subdirectory(src/base)
add_subdirectory(src/data)
add_subdirectory(src/reader)
add_subdirectory(src/score)
add_subdirectory(src/loss)
add_subdirectory(src/solver)


