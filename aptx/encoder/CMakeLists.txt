add_library(aptx_aa_encoder_objects OBJECT
    src/api/encoder.c src/codec/analysis.c src/codec/quantize.c
    src/codec/allocate.c src/codec/pack.c src/stream/stream.c src/stream/mode_tables.c)
aptx_aa_objects(aptx_aa_encoder_objects)
target_link_libraries(aptx_aa_encoder_objects PRIVATE audio_common::core)
target_include_directories(aptx_aa_encoder_objects PRIVATE include src/include)
add_library(aptx_aa_encoder STATIC $<TARGET_OBJECTS:aptx_aa_encoder_objects>)
add_library(aptx_aa::encoder ALIAS aptx_aa_encoder)
target_link_libraries(aptx_aa_encoder PUBLIC aptx_aa::common audio_common::core)
target_include_directories(aptx_aa_encoder PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
