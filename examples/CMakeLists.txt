qt_add_executable(make-sample-images
    make_sample_images.cpp
    SyntheticImages.h
)

target_link_libraries(make-sample-images
    PRIVATE
        Qt6::Gui
)
