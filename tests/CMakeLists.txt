function(stringart_add_test name)
    qt_add_executable(${name} ${name}.cpp)
    target_include_directories(${name}
        PRIVATE
            ${PROJECT_SOURCE_DIR}/examples
    )
    target_link_libraries(${name}
        PRIVATE
            stringart_core
            Qt6::Test
    )
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

stringart_add_test(tst_geometry)
stringart_add_test(tst_imageprocessor)
stringart_add_test(tst_optimizer)
stringart_add_test(tst_export)
stringart_add_test(tst_project)
stringart_add_test(tst_playback)

if(STRINGART_BUILD_APP)
    stringart_add_test(tst_ui)
    target_link_libraries(tst_ui
        PRIVATE
            stringart_ui
            stringart_uiplugin
            Qt6::QuickControls2
    )
    set_tests_properties(tst_ui PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
    )
endif()
