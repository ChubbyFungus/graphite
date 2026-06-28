include(FetchContent)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.90.9
)
FetchContent_MakeAvailable(imgui)

add_executable(Graphite.NativeApp WIN32
    src/main.cpp
    src/engine_document.cpp
    src/debug_panel.cpp
    src/input_adapter.cpp
    src/realtime_stylus_adapter.cpp
    src/pen_input_diagnostics.cpp
    src/d3d12_renderer.cpp
    src/cuda_graphite_backend.cu
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
)

target_include_directories(Graphite.NativeApp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_compile_definitions(Graphite.NativeApp PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

set_target_properties(Graphite.NativeApp PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
    CUDA_ARCHITECTURES native
)

target_link_libraries(Graphite.NativeApp PRIVATE
    d3d12
    d3dcompiler
    dxgi
    dxguid
    comdlg32
    windowscodecs
    ole32
    oleaut32
    uuid
    cudart
)

add_executable(Graphite.DocumentTests
    src/engine_document.cpp
    tests/document_replay_tests.cpp
)

target_include_directories(Graphite.DocumentTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_test(NAME Graphite.DocumentTests COMMAND Graphite.DocumentTests)

add_executable(Graphite.MaterialModelTests
    tests/material_model_tests.cpp
)

target_include_directories(Graphite.MaterialModelTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_test(NAME Graphite.MaterialModelTests COMMAND Graphite.MaterialModelTests)

add_executable(Graphite.CudaMaterialSmokeTests
    tests/cuda_material_smoke_tests.cu
)

set_target_properties(Graphite.CudaMaterialSmokeTests PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
    CUDA_ARCHITECTURES native
)

target_link_libraries(Graphite.CudaMaterialSmokeTests PRIVATE
    cudart
)

add_test(NAME Graphite.CudaMaterialSmokeTests COMMAND Graphite.CudaMaterialSmokeTests)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(
        NAME Graphite.PenDiagnosticsAnalyzer
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tools/analyze_pen_input_diagnostics.py
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/sample_pen_input_diagnostics.csv
            --require-pressure-variation
            --require-tilt
            --require-rotation
            --require-eraser
            --require-barrel
            --require-wm-pointer
    )
    add_test(
        NAME Graphite.MaterialCalibrationAnalyzer
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tools/analyze_material_calibration.py
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/sample_material_calibration.csv
    )
    add_test(
        NAME Graphite.MaterialCalibrationManifestCheck
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tools/check_material_calibration_manifest.py
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/sample_material_capture_manifest.csv
            --require-ready
    )
    add_test(
        NAME Graphite.EvidenceManifestCheck
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tools/check_validation_evidence_manifest.py
            ${CMAKE_SOURCE_DIR}/docs/native_validation_evidence_manifest.md
            --allow-missing
    )
endif()
