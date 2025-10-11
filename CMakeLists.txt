cmake_minimum_required(VERSION 3.10)
project(ParameterTraitsPart3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(ParameterTraitsPart3
    main.cpp
    ParameterTraits.h
)
