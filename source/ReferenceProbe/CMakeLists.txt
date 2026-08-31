cmake_minimum_required(VERSION 3.25)

project(IceCreamReferenceProbe VERSION 4.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "The IceCream reference probe is Windows-only.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
        "The IceCream reference probe must be configured with -A Win32.")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(IceCreamReferenceProbe ReferenceProbe.cpp)

target_compile_features(IceCreamReferenceProbe PRIVATE cxx_std_17)

target_compile_definitions(IceCreamReferenceProbe
    PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX)

if(MSVC)
    target_compile_options(IceCreamReferenceProbe PRIVATE /W4 /permissive-)
endif()
