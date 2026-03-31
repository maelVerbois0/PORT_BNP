# Minimum CMake version
cmake_minimum_required(VERSION 3.20)

# C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Project information
set(PROJECT_VERSION 0.1)
project(tusp VERSION ${PROJECT_VERSION} DESCRIPTION "Train routing using CG")

if (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR})

# Définition par défaut du type de build
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release")
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

if (NOT DEFINED ENV{GUROBI_HOME} AND NOT DEFINED GUROBI_DIR)
    message(FATAL_ERROR "No cache variable named GUROBI_DIR is defined, and GUROBI_HOME was not found in the environment variables. Please, define one of them.")
endif ()

# Source code
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

# Linking to Gurobi
include_directories(${GUROBI_INCLUDE_DIRS})
target_link_libraries(tusp_core PUBLIC gurobi)

################# Executables #################
# Create the column generation executable
add_executable(benchmark_tusp_solver src/benchmark_B&P.cpp)
target_link_libraries(benchmark_tusp_solver PUBLIC tusp_core)

add_executable(cli_tusp_solver src/cli_B&P.cpp)
target_link_libraries(cli_tusp_solver PUBLIC tusp_core)


################# Build Configurations #################

# --- MODE DEBUG ---
if(CMAKE_BUILD_TYPE MATCHES Debug)
    message(STATUS "Applying Debug options (Sanitizers, -O0)")
    
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(benchmark_tusp_solver PRIVATE 
            -g                      # Generate debug information
            -O0                     # Disable optimizations
            -fsanitize=address      # Enable AddressSanitizer
            -fno-omit-frame-pointer # Cleaner stack traces
        )
        target_link_options(benchmark_tusp_solver PRIVATE -fsanitize=address)
    endif()
endif()

# --- MODE RELEASE (OPTIMISÉ POUR BENCHMARK) ---
if(CMAKE_BUILD_TYPE MATCHES Release)
    message(STATUS "Applying aggressive optimizations for Benchmark (Release mode)")

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # 1. Flags d'optimisation maximale
        target_compile_options(tusp_core PRIVATE -O3 -DNDEBUG -march=native -w -pthread)
        target_compile_options(benchmark_tusp_solver PRIVATE -O3 -DNDEBUG -march=native -w -pthread)
        
        # 2. Activation du Link Time Optimization (LTO)
        include(CheckIPOSupported)
        check_ipo_supported(RESULT lto_supported)
        if(lto_supported)
            message(STATUS "LTO (Link Time Optimization) supporté et activé !")
            set_target_properties(tusp_core PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
            set_target_properties(benchmark_tusp_solver PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
        else()
            message(WARNING "LTO n'est pas supporté par ce compilateur.")
        endif()
    
    elseif(MSVC) # Si tu compiles sous Windows avec MSVC
        target_compile_options(tusp_core PRIVATE /O2 /Ob2 /DNDEBUG /GL /w)
        target_compile_options(benchmark_tusp_solver PRIVATE /O2 /Ob2 /DNDEBUG /GL /w)
        target_link_options(benchmark_tusp_solver PRIVATE /LTCG)
    endif()
endif()



# --- MODE RELEASE (CLI)---
if(CMAKE_BUILD_TYPE MATCHES Release)
    message(STATUS "Applying aggressive optimizations")

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # 1. Flags d'optimisation maximale
        target_compile_options(tusp_core PRIVATE -O3 -DNDEBUG -march=native -w -pthread)
        target_compile_options(cli_tusp_solver PRIVATE -O3 -DNDEBUG -march=native -w -pthread)
        
        # 2. Activation du Link Time Optimization (LTO)
        include(CheckIPOSupported)
        check_ipo_supported(RESULT lto_supported)
        if(lto_supported)
            message(STATUS "LTO (Link Time Optimization) supporté et activé !")
            set_target_properties(tusp_core PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
            set_target_properties(cli_tusp_solver PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
        else()
            message(WARNING "LTO n'est pas supporté par ce compilateur.")
        endif()
    
    elseif(MSVC) # Si tu compiles sous Windows avec MSVC
        target_compile_options(tusp_core PRIVATE /O2 /Ob2 /DNDEBUG /GL /w)
        target_compile_options(cli_tusp_solver PRIVATE /O2 /Ob2 /DNDEBUG /GL /w)
        target_link_options(cli_tusp_solver PRIVATE /LTCG)
    endif()
endif()