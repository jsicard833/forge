find_package(Catch2 3 REQUIRED)

add_executable(forge_tests
    vector_tests.cpp
)

target_link_libraries(forge_tests PRIVATE 
    forge::forge
    Catch2::Catch2WithMain
)

target_link_libraries(
    forge_tests
    PRIVATE
    Catch2::Catch2WithMain
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(forge_tests PRIVATE 
        -g -O0 -fno-omit-frame-pointer
        -fsanitize=address,undefined
    )
    target_link_options(forge_tests PRIVATE 
        -fsanitize=address,undefined
    )
endif()

include(Catch)
catch_discover_tests(forge_tests)