CMAKE_MINIMUM_REQUIRED(VERSION 3.24)

SET(CMAKE_CXX_STANDARD 20)
SET(CMAKE_COLOR_DIAGNOSTICS ON)


SET(INCLUDE_FILES
        memory_pool.hpp
        allocator.hpp
        pool_reporter.hpp
        pool_concept.hpp)

SET(ALLOCATOR_NAME allocator)


ADD_LIBRARY(${ALLOCATOR_NAME}
        INTERFACE
        ${INCLUDE_FILES})

IF (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")

    SET(WARN_COMPILER_OPTIONS "-Weverything -Wno-c++98-compat -Wno-c++98-compat-pedantic -Wno-padded -Wno-reserved-identifier -Wno-poison-system-directories")
    SET(OTHER_COMPILER_OPTIONS "")
    SET(COMPILER_OPTIONS "${WARN_COMPILER_OPTIONS}  ${OTHER_COMPILER_OPTIONS}")


    SET_SOURCE_FILES_PROPERTIES(
            ${INCLUDE_FILES}
            PROPERTIES
            COMPILE_FLAGS "${COMPILER_OPTIONS}")
ENDIF ()