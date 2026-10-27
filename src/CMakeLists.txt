# Computational engine. Depends only on Qt Core/Gui (QImage) and Qt Concurrent,
# never on Qt Quick, so it can be reused by the CLI, tests and benchmarks.
qt_add_library(stringart_core STATIC
    core/ColorRenderer.cpp
    core/ColorRenderer.h
    core/Compositing.cpp
    core/Compositing.h
    core/ExportManager.cpp
    core/ExportManager.h
    core/GrayImage.h
    core/ImageProcessor.cpp
    core/ImageProcessor.h
    core/LineCache.cpp
    core/LineCache.h
    core/LineRasterizer.cpp
    core/LineRasterizer.h
    core/OptimizationConfig.cpp
    core/OptimizationConfig.h
    core/OptimizationProblem.cpp
    core/OptimizationProblem.h
    core/PinGeometry.cpp
    core/PinGeometry.h
    core/PlaybackRenderer.cpp
    core/PlaybackRenderer.h
    core/ProjectFile.cpp
    core/ProjectFile.h
    core/StringArtResult.cpp
    core/StringArtResult.h
    core/ThreadRenderer.cpp
    core/ThreadRenderer.h
    core/WorkshopDocuments.cpp
    core/WorkshopDocuments.h
    algorithms/GreedyOptimizer.cpp
    algorithms/GreedyOptimizer.h
    algorithms/IOptimizer.h
    algorithms/LayeredColorOptimizer.cpp
    algorithms/LayeredColorOptimizer.h
    algorithms/PaletteSuggester.cpp
    algorithms/PaletteSuggester.h
)

target_include_directories(stringart_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(stringart_core
    PUBLIC
        Qt6::Core
        Qt6::Gui
    PRIVATE
        Qt6::Concurrent
)

if(MSVC)
    target_compile_options(stringart_core PRIVATE /W4)
else()
    target_compile_options(stringart_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_subdirectory(cli)

if(STRINGART_BUILD_APP)
    add_subdirectory(app)
endif()
