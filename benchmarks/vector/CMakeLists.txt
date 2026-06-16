add_executable(forge_vector_benchmarks
    bench_construction.cpp
    bench_insertion.cpp
    bench_deletion.cpp
    bench_access.cpp
)

target_link_libraries(forge_vector_benchmarks PRIVATE 
    forge
    benchmark::benchmark_main
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(forge_vector_benchmarks PRIVATE
        -O3 -march=native -DNDEBUG -fomit-frame-pointer 
    )
endif()