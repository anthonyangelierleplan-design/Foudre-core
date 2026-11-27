cmake_minimum_required(VERSION 3.22)
project(FoudreCore VERSION 0.1.0)

# Foudre-Core : le tronc commun de la gamme Foudre Audio.
#  - core/ : code partage par tous les plugins (pour l'instant : les regles de la gamme, FoudreRegles.h)
#  - banc/ : foudre-banc, le banc d'essai qui charge un .vst3 sans Ableton

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT TARGET juce::juce_core)          # un plugin qui inclut Foudre-Core apporte deja JUCE
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# Code partage : un plugin fait  target_link_libraries(MonPlugin PRIVATE foudre::core)
add_library(foudre_core INTERFACE)
add_library(foudre::core ALIAS foudre_core)
target_include_directories(foudre_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/core)

option(FOUDRE_BANC "Compiler le banc d'essai foudre-banc" ${PROJECT_IS_TOP_LEVEL})
if(FOUDRE_BANC)
    add_subdirectory(banc)
endif()
