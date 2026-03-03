# Set the minimum required version of CMake for this project.
cmake_minimum_required(VERSION 3.13)

# set aws cpp sdk pathname
set(CMAKE_PREFIX_PATH "/Users/ryanhamby/Desktop/aws-sdk-cpp-install")

# Set the AWS service components used by this project.
set(SERVICE_COMPONENTS s3 dynamodb)

set(CMAKE_CXX_COMPILER g++)
set(CMAKE_C_COMPILER gcc)

# Set this project's name.
project("InvertedYieldCurveTrader")

# Set the C++ standard to use to build this target.
# At least C++ 11 is required for the AWS SDK for C++.
set(CMAKE_CXX_STANDARD 20)

# Use the MSVC variable to determine if this is a Windows build.
set(WINDOWS_BUILD ${MSVC})

file(GLOB DATA_PROCESSORS_SRC
     "*DataProcessors/*.cpp"
)

file(GLOB AWS_CLIENTS_SRC
     "*AwsClients/*.cpp"
)
# DynamoDB support enabled for tiered storage (hot/cold data partitioning)

file(GLOB UTILS_SRC
     "*Utils/*.cpp"
)

file(GLOB DATA_PROVIDERS_SRC
     "*DataProviders/*.cpp"
)

file(GLOB DATA_ALIGNMENT_SRC
     "*DataProcessors/DataAligner.cpp"
)

if (WINDOWS_BUILD) # Set the location where CMake can find the installed libraries for the AWS SDK.
    string(REPLACE ";" "/aws-cpp-sdk-all;" SYSTEM_MODULE_PATH "${CMAKE_SYSTEM_PREFIX_PATH}/aws-cpp-sdk-all")
    list(APPEND CMAKE_PREFIX_PATH ${SYSTEM_MODULE_PATH})
endif ()

# Find the AWS SDK for C++ package.
find_package(AWSSDK REQUIRED COMPONENTS ${SERVICE_COMPONENTS})
find_package(GTest REQUIRED)

if (WINDOWS_BUILD)
    # Copy relevant AWS SDK for C++ libraries into the current binary directory for running and debugging.

    # set(BIN_SUB_DIR "/Debug") # if you are building from the command line you may need to uncomment this
    # and set the proper subdirectory to the executables' location.

    AWSSDK_CPY_DYN_LIBS(SERVICE_COMPONENTS "" ${CMAKE_CURRENT_BINARY_DIR}${BIN_SUB_DIR})
endif ()

set(TESTS_PARENT_DIR "${CMAKE_SOURCE_DIR}/..")
file(GLOB TEST_SOURCES
    "${TESTS_PARENT_DIR}/test/*Test.cpp"
    )
add_executable(test ${TEST_SOURCES}
        ${DATA_PROCESSORS_SRC}
        ${DATA_PROVIDERS_SRC}
        ${UTILS_SRC})
add_executable(${PROJECT_NAME}
        MultiVarAnalysisWorkflow.cpp
        ${DATA_PROCESSORS_SRC}
        ${AWS_CLIENTS_SRC}
        ${UTILS_SRC}
        ${DATA_PROVIDERS_SRC}
        ${DATA_ALIGNMENT_SRC})

include_directories("${CMAKE_SOURCE_DIR}/src")
include_directories(${AWSSDK_INCLUDE_DIRS})
include_directories(${GTEST_INCLUDE_DIRS})
include_directories("${CMAKE_SOURCE_DIR}/include")
include_directories("/opt/homebrew/opt/nlohmann-json/include")
include_directories("/opt/homebrew/opt/eigen/include/eigen3")
include_directories("/usr/local/include")

target_link_libraries(${PROJECT_NAME}
        ${AWSSDK_LINK_LIBRARIES}
        ${AWSSDK_LIBRARIES}
        curl
        -static-libgcc
        -static-libstdc++)
        
target_link_libraries(test
        ${AWSSDK_LINK_LIBRARIES}
        ${AWSSDK_LIBRARIES}
        ${GTEST_BOTH_LIBRARIES}
        curl)
