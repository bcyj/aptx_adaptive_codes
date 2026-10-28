add_library(aptx_aa_common_objects OBJECT
    src/api/runtime.c src/api/util.c src/codec/params.c src/dsp/engine.c src/dsp/predict.c src/tables/codec_tables.c)
aptx_aa_objects(aptx_aa_common_objects)
target_link_libraries(aptx_aa_common_objects PRIVATE audio_common::core)
add_library(aptx_aa_common STATIC $<TARGET_OBJECTS:aptx_aa_common_objects>)
add_library(aptx_aa::common ALIAS aptx_aa_common)
target_link_libraries(aptx_aa_common PUBLIC audio_common::core)
target_include_directories(aptx_aa_common PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_compile_definitions(aptx_aa_common PUBLIC
    APTX_AA_NO_CRT=$<BOOL:${APTX_AA_NO_CRT}>
    APTX_AA_DRIVER_RESOURCES=$<BOOL:${APTX_AA_DRIVER_RESOURCES}>
    APTX_AA_EXTERNAL_ALLOCATOR_ONLY=$<BOOL:${APTX_AA_EXTERNAL_ALLOCATOR_ONLY}>)
