// Copyright Epic Games, Inc. All Rights Reserved.

/*================================================================================================
	StrataDefinitions.ush: used in ray tracing shaders and C++ code to define common constants
	!!! Changing this file requires recompilation of the engine !!!
=================================================================================================*/

#pragma once

// Change this to force recompilation of all strata dependent shaders (use https://www.random.org/cgi-bin/randbyte?nbytes=4&format=h)
#define STRATA_SHADER_VERSION 0xc9624d0e 



#define STRATA_MAX_BSDF_COUNT_PER_LAYER		4
#define STRATA_MAX_LAYER_COUNT				4

// It this is changed, STATE_BITS_SHAREDNORMAL also needs to be updated
#define STRATA_MAX_SHARED_NORMAL_REGISTERS	4

#define STRATA_PACKED_NORMAL_STRIDE_BYTES	4


#define STRATA_BSDF_TYPE_SLAB				0
#define STRATA_BSDF_TYPE_SHEEN				1
#define STRATA_BSDF_TYPE_VOLUMETRICFOGCLOUD	2
#define STRATA_BSDF_TYPE_UNLIT				3
#define STRATA_BSDF_TYPE_HAIR				4
#define STRATA_BSDF_TYPE_SINGLELAYERWATER	5
// When more than 8 BSDF must exists, please update STATE_BITS_BSDF and FStrataClassification.ShadingModels packing in Strata.ush

#define STRATA_DATA_TILE_SIZE				8
#define STRATA_DATA_TILE_SIZE_DIV_AS_SHIFT	3
