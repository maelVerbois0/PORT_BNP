#Minimum CMake version
cmake_minimum_required(VERSION 3.20)

# C++ standard
set(CMAKE_CXX_STANDARD 23)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	set(CMAKE_CXX_FLAGS  "/O2 /w  /DNDEBUG")
else()
	set(CMAKE_CXX_FLAGS  " -w  -pthread")
endif()


#Project information
set(PROJECT_VERSION 0.1)
project(tusp VERSION ${PROJECT_VERSION} DESCRIPTION "Train routing using CG")
if (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR})
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release")
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

if (NOT DEFINED ENV{GUROBI_HOME} AND NOT DEFINED GUROBI_DIR)
    message(FATAL_ERROR "No cache variable named GUROBI_DIR is defined, and GUROBI_HOME was not found in the environment variables. Please, define one of them.")
endif ()

#Source code

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

include_directories(
    ${SRC_DIR}
    ${SRC_DIR}/parser
    ${SRC_DIR}/data
    ${SRC_DIR}/graph
    ${SRC_DIR}/builder
    ${SRC_DIR}/solver

    ${GUROBI_INC}
)

set(
    SOURCE_FILES_
    src/parser/GraphParser.cpp

    src/data/PLink.cpp
    src/data/PNode.cpp
    src/data/Service.cpp
    src/data/Train.cpp

    src/graph/Arc.cpp
    src/graph/Node.cpp
    src/graph/TimeSpaceGraph.cpp

    src/builder/GraphBuilder.cpp

    src/solver/MasterProblem.cpp
    src/solver/PricingProblem.cpp
    src/solver/GlobalStateManager.cpp
    src/solver/SearchTree.cpp
    src/solver/BranchAndPriceSolver.cpp

)
################# Libraries #################
# Add the core library
add_library(tusp_core ${SOURCE_FILES_})



# Finding Gurobi
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")
message("CMAKE_MODULE_PATH : ${CMAKE_MODULE_PATH}")
find_package(GUROBI REQUIRED)
# Linking to Gurobi
include_directories(${GUROBI_INCLUDE_DIRS})
target_link_libraries(tusp_core PUBLIC gurobi)


################# Executables #################
# Create the column generation executable
add_executable(tusp_solver src/test_column_generation.cpp)
target_link_libraries(tusp_solver PUBLIC tusp_core)