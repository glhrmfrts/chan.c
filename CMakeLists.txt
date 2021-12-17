cmake_minimum_required( VERSION 3.15 )

project( chan_test )

set( TARGET_NAME chan_test )

add_executable( ${TARGET_NAME} chan_test.c )

target_compile_options( ${TARGET_NAME} PRIVATE -std=c11 -Werror -Wall -Wextra -pedantic -g )

target_link_libraries( ${TARGET_NAME} PRIVATE pthread )
