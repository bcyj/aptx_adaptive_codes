add_library(aptx_aa_decoder_objects OBJECT
    src/api/decoder.c src/codec/bitstream.c src/codec/reconstruct.c
    src/dsp/synthesis.c src/dsp/synthesis_tables.c src/dsp/decoder_trees.c)
aptx_aa_objects(aptx_aa_decoder_objects)
target_compile_definitions(aptx_aa_decoder_objects PRIVATE
    APTX_AA_DECODER_FIXED_POINT=$<BOOL:${APTX_AA_DECODER_FIXED_POINT}>)
target_include_directories(aptx_aa_decoder_objects PRIVATE include src/include)
add_library(aptx_aa_decoder STATIC $<TARGET_OBJECTS:aptx_aa_decoder_objects>)
add_library(aptx_aa::decoder ALIAS aptx_aa_decoder)
target_link_libraries(aptx_aa_decoder PUBLIC aptx_aa::common)
target_compile_definitions(aptx_aa_decoder PUBLIC
    APTX_AA_DECODER_FIXED_POINT=$<BOOL:${APTX_AA_DECODER_FIXED_POINT}>)
target_include_directories(aptx_aa_decoder PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
