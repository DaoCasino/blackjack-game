cmake_minimum_required(VERSION 3.5)

project(blackjack_contract)

add_subdirectory(${GAME_SDK_PATH}/sdk ${CMAKE_BINARY_DIR}/sdk)

add_game_contract(blackjack src/blackjack.cpp)
target_include_directories(blackjack PUBLIC include/)
