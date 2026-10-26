qt_add_executable(stringart-cli
    main.cpp
)

target_compile_definitions(stringart-cli
    PRIVATE
        PROJECT_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(stringart-cli
    PRIVATE
        stringart_core
)
