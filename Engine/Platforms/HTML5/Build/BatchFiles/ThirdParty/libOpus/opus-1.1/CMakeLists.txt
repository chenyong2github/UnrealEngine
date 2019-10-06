
# this file originally came from: https://github.com/eppixx/opus-cmake
# modified slightly for path fixes


cmake_minimum_required(VERSION 3.1)
project(opus)

option(OPUS_USE_ALLOCA "USE_ALLOCA" ON)
option(OPUS_FIXED_POINT "FIXED_POINT" OFF)
option(OPUS_DISABLE_FLOAT_API "DISABLE_FLOAT_API" OFF)

set(OPUS_INCLUDE_DIR
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set(SILK_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/CNG.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/code_signs.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/init_decoder.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/decode_core.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/decode_frame.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/decode_parameters.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/decode_indices.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/decode_pulses.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/decoder_set_fs.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/dec_API.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/enc_API.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/encode_indices.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/encode_pulses.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/gain_quant.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/interpolate.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/LP_variable_cutoff.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/NLSF_decode.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/NSQ.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/NSQ_del_dec.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/PLC.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/shell_coder.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/tables_gain.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/tables_LTP.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/tables_NLSF_CB_NB_MB.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/tables_NLSF_CB_WB.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/tables_other.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/tables_pitch_lag.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/tables_pulses_per_block.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/VAD.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/control_audio_bandwidth.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/quant_LTP_gains.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/VQ_WMat_EC.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/HP_variable_cutoff.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/NLSF_encode.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/NLSF_VQ.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/NLSF_unpack.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/NLSF_del_dec_quant.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/process_NLSFs.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/stereo_LR_to_MS.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/stereo_MS_to_LR.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/check_control_input.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/control_SNR.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/init_encoder.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/control_codec.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/A2NLSF.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/ana_filt_bank_1.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/biquad_alt.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/bwexpander_32.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/bwexpander.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/debug.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/decode_pitch.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/inner_prod_aligned.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/lin2log.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/log2lin.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/LPC_analysis_filter.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/LPC_inv_pred_gain.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/table_LSF_cos.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/NLSF2A.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/NLSF_stabilize.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/NLSF_VQ_weights_laroia.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/pitch_est_tables.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/resampler.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/resampler_down2_3.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/resampler_down2.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/resampler_private_AR2.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/resampler_private_down_FIR.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/resampler_private_IIR_FIR.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/resampler_private_up2_HQ.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/resampler_rom.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/sigm_Q15.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/sort.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/sum_sqr_shift.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/stereo_decode_pred.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/stereo_encode_pred.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/stereo_find_predictor.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/stereo_quant_pred.c
)

set(SILK_SOURCES_SSE4_1
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/x86/NSQ_sse.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/x86/NSQ_del_dec_sse.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/x86/x86_silk_map.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/x86/VAD_sse.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/x86/VQ_WMat_EC_sse.c
)

set(SILK_SOURCES_ARM_NEON_INTR
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/arm/arm_silk_map.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/arm/NSQ_del_dec_neon_intr.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/arm/NSQ_neon.c
)

set(SILK_SOURCES_FIXED
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/fixed/LTP_analysis_filter_FIX.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/fixed/LTP_scale_ctrl_FIX.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/fixed/corrMatrix_FIX.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/fixed/encode_frame_FIX.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/fixed/find_LPC_FIX.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/fixed/find_LTP_FIX.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/fixed/find_pitch_lags_FIX.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/fixed/find_pred_coefs_FIX.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/fixed/noise_shape_analysis_FIX.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/silk/fixed/prefilter_FIX.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/fixed/process_gains_FIX.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/fixed/regularize_correlations_FIX.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/fixed/residual_energy16_FIX.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/fixed/residual_energy_FIX.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/fixed/solve_LS_FIX.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/fixed/warped_autocorrelation_FIX.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/fixed/apply_sine_window_FIX.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/fixed/autocorr_FIX.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/fixed/burg_modified_FIX.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/fixed/k2a_FIX.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/fixed/k2a_Q16_FIX.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/fixed/pitch_analysis_core_FIX.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/fixed/vector_ops_FIX.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/fixed/schur64_FIX.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/fixed/schur_FIX.c
)

set(SILK_SOURCES_FIXED_SSE4_1
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/fixed/x86/vector_ops_FIX_sse.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/fixed/x86/burg_modified_FIX_sse.c
)

set(SILK_SOURCES_FLOAT
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/float/apply_sine_window_FLP.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/float/corrMatrix_FLP.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/float/encode_frame_FLP.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/float/find_LPC_FLP.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/float/find_LTP_FLP.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/float/find_pitch_lags_FLP.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/float/find_pred_coefs_FLP.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/float/LPC_analysis_filter_FLP.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/float/LTP_analysis_filter_FLP.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/float/LTP_scale_ctrl_FLP.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/float/noise_shape_analysis_FLP.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/float/prefilter_FLP.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/float/process_gains_FLP.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/float/regularize_correlations_FLP.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/float/residual_energy_FLP.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/float/solve_LS_FLP.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/float/warped_autocorrelation_FLP.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/float/wrappers_FLP.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/float/autocorrelation_FLP.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/float/burg_modified_FLP.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/float/bwexpander_FLP.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/float/energy_FLP.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/float/inner_product_FLP.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/float/k2a_FLP.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/float/levinsondurbin_FLP.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/float/LPC_inv_pred_gain_FLP.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/float/pitch_analysis_core_FLP.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/float/scale_copy_vector_FLP.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/float/scale_vector_FLP.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/float/schur_FLP.c
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/float/sort_FLP.c
)

set(CELT_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/celt/bands.c
  ${CMAKE_CURRENT_SOURCE_DIR}/celt/celt.c
  ${CMAKE_CURRENT_SOURCE_DIR}/celt/celt_encoder.c
  ${CMAKE_CURRENT_SOURCE_DIR}/celt/celt_decoder.c
  ${CMAKE_CURRENT_SOURCE_DIR}/celt/cwrs.c
  ${CMAKE_CURRENT_SOURCE_DIR}/celt/entcode.c
  ${CMAKE_CURRENT_SOURCE_DIR}/celt/entdec.c
  ${CMAKE_CURRENT_SOURCE_DIR}/celt/entenc.c
  ${CMAKE_CURRENT_SOURCE_DIR}/celt/kiss_fft.c
  ${CMAKE_CURRENT_SOURCE_DIR}/celt/laplace.c
  ${CMAKE_CURRENT_SOURCE_DIR}/celt/mathops.c
  ${CMAKE_CURRENT_SOURCE_DIR}/celt/mdct.c
  ${CMAKE_CURRENT_SOURCE_DIR}/celt/modes.c
  ${CMAKE_CURRENT_SOURCE_DIR}/celt/pitch.c
  ${CMAKE_CURRENT_SOURCE_DIR}/celt/celt_lpc.c
  ${CMAKE_CURRENT_SOURCE_DIR}/celt/quant_bands.c
  ${CMAKE_CURRENT_SOURCE_DIR}/celt/rate.c
  ${CMAKE_CURRENT_SOURCE_DIR}/celt/vq.c
)

set(CELT_SOURCES_SSE
  ${CMAKE_CURRENT_SOURCE_DIR}/celt/x86/x86cpu.c
  ${CMAKE_CURRENT_SOURCE_DIR}/celt/x86/x86_celt_map.c
  ${CMAKE_CURRENT_SOURCE_DIR}/celt/x86/pitch_sse.c
)

set(CELT_SOURCES_SSE2
  ${CMAKE_CURRENT_SOURCE_DIR}/celt/x86/pitch_sse2.c
  ${CMAKE_CURRENT_SOURCE_DIR}/celt/x86/vq_sse2.c
)

set(CELT_SOURCES_SSE4_1
  ${CMAKE_CURRENT_SOURCE_DIR}/celt/x86/celt_lpc_sse.c
  ${CMAKE_CURRENT_SOURCE_DIR}/celt/x86/pitch_sse4_1.c
)

set(CELT_SOURCES_ARM
  ${CMAKE_CURRENT_SOURCE_DIR}/celt/arm/armcpu.c
  ${CMAKE_CURRENT_SOURCE_DIR}/celt/arm/arm_celt_map.c
)

set(CELT_SOURCES_ARM_ASM
  ${CMAKE_CURRENT_SOURCE_DIR}/celt/arm/celt_pitch_xcorr_arm.s
)

set(CELT_AM_SOURCES_ARM_ASM
  ${CMAKE_CURRENT_SOURCE_DIR}/celt/arm/armopts.s.in
)

set(CELT_SOURCES_ARM_NEON_INTR
  ${CMAKE_CURRENT_SOURCE_DIR}/celt/arm/celt_neon_intr.c
)

set(CELT_SOURCES_ARM_NE10
  ${CMAKE_CURRENT_SOURCE_DIR}/celt/arm/celt_ne10_fft.c
  ${CMAKE_CURRENT_SOURCE_DIR}/celt/arm/celt_ne10_mdct.c
)

set(OPUS_HEADER
  ${CMAKE_CURRENT_SOURCE_DIR}/include/opus.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/opus_custom.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/opus_defines.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/opus_multistream.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/opus_types.h
)

set(OPUS_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/opus.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/opus_decoder.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/opus_encoder.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/opus_multistream.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/opus_multistream_encoder.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/opus_multistream_decoder.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/repacketizer.c
)

set(OPUS_SOURCES_FLOAT
  ${CMAKE_CURRENT_SOURCE_DIR}/src/analysis.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/mlp.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/mlp_data.c
)

if(OPUS_FIXED_POINT)
	list(APPEND SILK_SOURCES ${SILK_SOURCES_FIXED})
	if(HAVE_SSE4_1)
		list(APPEND SILK_SOURCES ${SILK_SOURCES_SSE4_1} ${SILK_SOURCES_FIXED_SSE4_1})
	endif(HAVE_SSE4_1)
else(OPUS_FIXED_POINT)
	list(APPEND SILK_SOURCES ${SILK_SOURCES_FLOAT})
	if (HAVE_SSE4_1)
		list(APPEND SILK_SOURCES ${SILK_SOURCES_SSE4_1})
	endif(HAVE_SSE4_1)
endif(OPUS_FIXED_POINT)

if (NOT OPUS_DISABLE_FLOAT_API)
	list(APPEND OPUS_SOURCES ${OPUS_SOURCES_FLOAT})
endif(NOT OPUS_DISABLE_FLOAT_API)

if(HAVE_SSE)
	list(APPEND CELT_SOURCES ${CELT_SOURCES_SSE})
endif(HAVE_SSE)

if(HAVE_SSE2)
	list(APPEND CELT_SOURCES ${CELT_SOURCES_SSE2})
endif(HAVE_SSE2)

if(HAVE_SSE4_1)
	list(APPEND CELT_SOURCES ${CELT_SOURCES_SSE4_1})
endif(HAVE_SSE4_1)

if(CPU_ARM)
	list(APPEND CELT_SOURCES ${CELT_SOURCES_ARM})
	list(APPEND SILK_SOURCES ${SILK_SOURCES_ARM})

	if(HAVE_ARM_NEON_INTR)
		list(APPEND CELT_SOURCES ${CELT_SOURCES_ARM_NEON_INTR})
		list(APPEND SILK_SOURCES ${SILK_SOURCES_ARM_NEON_INTR})
	endif(HAVE_ARM_NEON_INTR)

	if(HAVE_ARM_NE10)
		list(APPEND CELT_SOURCES ${CELT_SOURCES_ARM_NE10})
	endif(HAVE_ARM_NE10)

	#if(OPUS_ARM_EXTERNAL_ASM)
	#	noinst_LTLIBRARIES = libarmasm.la
	#	libarmasm_la_SOURCES = $(CELT_SOURCES_ARM_ASM:.s=-gnu.S)
	#	BUILT_SOURCES = $(CELT_SOURCES_ARM_ASM:.s=-gnu.S) \
	#	$(CELT_AM_SOURCES_ARM_ASM:.s.in=.s) \
	#	$(CELT_AM_SOURCES_ARM_ASM:.s.in=-gnu.S)
	#endif(OPUS_ARM_EXTERNAL_ASM)
endif(CPU_ARM)

# It is strongly recommended to uncomment one of these
# VAR_ARRAYS: Use C99 variable-length arrays for stack allocation
# USE_ALLOCA: Use alloca() for stack allocation
# If none is defined, then the fallback is a non-threadsafe global array
if (OPUS_USE_ALLOCA)
  add_definitions(-DUSE_ALLOCA)
else (OPUS_USE_ALLOCA)
  add_definitions(-DVAR_ARRAYS)
endif (OPUS_USE_ALLOCA)

# These options affect performance
# HAVE_LRINTF: Use C99 intrinsics to speed up float-to-int conversion
add_definitions(-DHAVE_LRINTF)

add_definitions(-DOPUS_BUILD)

if (OPUS_FIXED_POINT)
  add_definitions(-DFIXED_POINT=1 -DDISABLE_FLOAT_API)
endif (OPUS_FIXED_POINT)


add_library(opus STATIC ${CELT_SOURCES} ${SILK_SOURCES} ${OPUS_SOURCES})
target_compile_options(opus PRIVATE $<$<OR:$<C_COMPILER_ID:Clang>,$<C_COMPILER_ID:GNU>>:
  -Wall
  -Wextra
  -Wcast-align
  -Wnested-externs
  -Wshadow
  -Wstrict-prototypes
>)
target_include_directories(opus PUBLIC ${OPUS_INCLUDE_DIR})
target_include_directories(opus PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/silk
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/float
  ${CMAKE_CURRENT_SOURCE_DIR}/silk/fixed
  ${CMAKE_CURRENT_SOURCE_DIR}/celt
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
