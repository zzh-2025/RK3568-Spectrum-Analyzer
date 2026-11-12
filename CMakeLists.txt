cmake_minimum_required(VERSION 3.16)

project(SpectrumAnalysisCpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Shared algorithm implementation.  Build it once and reuse it in all CLI tools.
# Keep the optimized implementation under the existing src/multilorentz.cpp name.
add_library(spectrum_core STATIC
    src/lorentz.cpp
    src/multilorentz.cpp
    src/autopeak.cpp
    src/model_selection.cpp
    src/baseline_compare.cpp
)

target_include_directories(spectrum_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_compile_options(spectrum_core PRIVATE
    -Wall -Wextra -Wpedantic
)

# Each target has its own main() in apps/ or tests/.
# PUBLIC include path above propagates to these executables.
function(add_spectrum_program target source_file)
    add_executable(${target} "${source_file}")
    target_link_libraries(${target} PRIVATE spectrum_core)
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic
    )
endfunction()

# Original single-spectrum and batch programs.
add_spectrum_program(spectrum_reader       apps/main.cpp)
add_spectrum_program(spectrum_batch        apps/batch_analysis.cpp)

# Standalone algorithm tests (preserve existing target names).
add_spectrum_program(multi_lorentz_test    tests/multi_lorentz_test.cpp)
add_spectrum_program(auto_peak_test        tests/auto_peak_test.cpp)
add_spectrum_program(model_selection_test  tests/model_selection_test.cpp)
add_spectrum_program(baseline_compare_test tests/baseline_compare_test.cpp)

# Qt is intentionally not changed here.  Its own CMake file may need source
# path updates later if it references the former cpp-version/*.cpp locations.

add_spectrum_program(fit_compare_test tests/fit_compare_test.cpp)

# Stage 7: batch fit + coupling analysis
target_sources(spectrum_core PRIVATE
    src/coupling_analysis.cpp
)

add_spectrum_program(coupling_batch
    apps/coupling_batch.cpp
)

add_spectrum_program(coupling_analysis_test
    tests/coupling_analysis_test.cpp
)
