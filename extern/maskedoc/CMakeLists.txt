set(MOC_SOURCES
    MaskedOcclusionCulling.cpp
    MaskedOcclusionCullingAVX2.cpp
    MaskedOcclusionCullingAVX512.cpp
)

add_library(maskedoc STATIC ${MOC_SOURCES})
target_include_directories(maskedoc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
    # Main file must be compiled at SSE2 baseline (no AVX)
    set_source_files_properties(MaskedOcclusionCulling.cpp PROPERTIES COMPILE_FLAGS "/arch:SSE2")
    set_source_files_properties(MaskedOcclusionCullingAVX2.cpp PROPERTIES COMPILE_FLAGS "/arch:AVX2")
    set_source_files_properties(MaskedOcclusionCullingAVX512.cpp PROPERTIES COMPILE_FLAGS "/arch:AVX512")
else()
    set_source_files_properties(MaskedOcclusionCulling.cpp PROPERTIES COMPILE_FLAGS "-msse2")
    set_source_files_properties(MaskedOcclusionCullingAVX2.cpp PROPERTIES COMPILE_FLAGS "-mavx2 -mfma")
    set_source_files_properties(MaskedOcclusionCullingAVX512.cpp PROPERTIES COMPILE_FLAGS "-mavx512f -mavx512bw")
endif()
