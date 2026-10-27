# UI layer as a static QML module so the Qt Quick integration test can load
# the same Main.qml as the application.
qt_add_library(stringart_ui STATIC)

qt_add_qml_module(stringart_ui
    URI StringArtStudio
    OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/qml/StringArtStudio
    QML_FILES
        qml/Main.qml
        qml/HelpLabel.qml
        qml/ImagePanel.qml
        qml/ParamSlider.qml
        qml/PlaybackPanel.qml
        qml/ProgressPanel.qml
        qml/SettingsPanel.qml
    SOURCES
        ImageStore.cpp
        ImageStore.h
        OptimizationWorker.cpp
        OptimizationWorker.h
        ProjectController.cpp
        ProjectController.h
)

target_include_directories(stringart_ui
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(stringart_ui
    PUBLIC
        stringart_core
        Qt6::Quick
    PRIVATE
        Qt6::Concurrent
)

qt_add_executable(stringart-studio
    main.cpp
)

target_link_libraries(stringart-studio
    PRIVATE
        stringart_ui
        stringart_uiplugin
        Qt6::QuickControls2
)

set_target_properties(stringart-studio PROPERTIES
    WIN32_EXECUTABLE TRUE
)

install(TARGETS stringart-studio stringart-cli
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
