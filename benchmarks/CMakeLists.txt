qt_add_executable(stringart-benchmark
    stringart_benchmark.cpp
)

target_include_directories(stringart-benchmark
    PRIVATE
        ${PROJECT_SOURCE_DIR}/examples
)

target_link_libraries(stringart-benchmark
    PRIVATE
        stringart_core
)

if(WIN32)
    target_link_libraries(stringart-benchmark PRIVATE psapi)
endif()
