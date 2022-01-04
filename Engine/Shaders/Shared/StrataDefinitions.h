// Copyright Epic Games, Inc. All Rights Reserved.

/*================================================================================================
	StrataDefinitions.ush: used in ray tracing shaders and C++ code to define common constants
	!!! Changing this file requires recompilation of the engine !!!
=================================================================================================*/

#pragma once

// Change this to force recompilation of all strata dependent shaders (use https://www.random.org/cgi-bin/randbyte?nbytes=4&format=h)
#define STRATA_SHADER_VERSION 0x04499eb2  



#define STRATA_MAX_BSDF_COUNT_PER_LAYER		4
#define STRATA_MAX_LAYER_COUNT				4
#define STRATA_MAX_BSDF_COUNT				15

// It this is changed, STATE_BIT_COUNT_SHAREDLOCALBASESID and HEADER_BIT_COUNT_SHAREDLOCALBASES_COUNT also needs to be updated
#define STRATA_MAX_SHAREDLOCALBASES_REGISTERS		4

#define STRATA_PACKED_SHAREDLOCALBASIS_STRIDE_BYTES	4


#define STRATA_BSDF_TYPE_SLAB				0
#define STRATA_BSDF_TYPE_VOLUMETRICFOGCLOUD	1
#define STRATA_BSDF_TYPE_UNLIT				2
#define STRATA_BSDF_TYPE_HAIR				3
#define STRATA_BSDF_TYPE_SINGLELAYERWATER	4
// When more than 5 BSDF must exists, please update STATE_BIT_COUNT_BSDF and FStrataClassification.ShadingModels packing in Strata.ush

// The size of strata material classification tiles on screen
#define STRATA_TILE_SIZE					8

// This is used to round up the strata material buffer for better cache locality on read/write.
#define STRATA_DATA_TILE_SIZE				8
#define STRATA_DATA_TILE_SIZE_DIV_AS_SHIFT	3

// The default thickness of a layer is considered to be 0.01 centimeter = 0.1 millimeter
#define STRATA_LAYER_DEFAULT_THICKNESS_CM	0.01f

// The thin lighting model is used when the thickness becomes strictly lower than 0.1 millimeter
#define STRATA_LAYER_ISTHIN_THICKNESS_THRESHOLD_CM 0.01f

#define STRATA_BASE_PASS_MRT_OUTPUT_COUNT	2
