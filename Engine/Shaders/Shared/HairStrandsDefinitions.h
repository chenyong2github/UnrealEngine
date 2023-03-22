// Copyright Epic Games, Inc. All Rights Reserved.

/*================================================================================================
	HairStrandsDefinitions.ush: used in ray tracing shaders and C++ code to define common constants
	!!! Changing this file requires recompilation of the engine !!!
=================================================================================================*/

#pragma once

// Change this to force recompilation of all strata dependent shaders (use https://www.random.org/cgi-bin/randbyte?nbytes=4&format=h)
#define HAIRSTRANDS_SHADER_VERSION 0x3ba5af0e

// Attribute index
#define HAIR_ATTRIBUTE_ROOTUV 0
#define HAIR_ATTRIBUTE_SEED 1
#define HAIR_ATTRIBUTE_LENGTH 2
#define HAIR_ATTRIBUTE_BASECOLOR 3
#define HAIR_ATTRIBUTE_ROUGHNESS 4
#define HAIR_ATTRIBUTE_AO 5
#define HAIR_ATTRIBUTE_CLUMPID 6
#define HAIR_ATTRIBUTE_CLUMPID3 7
#define HAIR_ATTRIBUTE_COUNT 8

// Groom limits (based on encoding)
#define HAIR_MAX_NUM_POINT_PER_CURVE ((1u<<8)-1u)
#define HAIR_MAX_NUM_POINT_PER_GROUP ((1u<<24)-1u)
#define HAIR_MAX_NUM_CURVE_PER_GROUP ((1u<<22)-1u)

#define HAIR_ATTRIBUTE_INVALID_OFFSET 0xFFFFFFFF

// Max number of discrete LOD that a hair group can have
#define MAX_HAIR_LOD 8

// Use triangle strip for HW raster path
#define USE_HAIR_TRIANGLE_STRIP 0

// Number of vertex per control-point
#if USE_HAIR_TRIANGLE_STRIP
#define HAIR_POINT_TO_VERTEX 2u
#else
#define HAIR_POINT_TO_VERTEX 6u
#endif

// Number of triangle per control-point
#define HAIR_POINT_TO_TRIANGLE 2u

// Type of control points
#define HAIR_CONTROLPOINT_INSIDE 0u
#define HAIR_CONTROLPOINT_START	 1u
#define HAIR_CONTROLPOINT_END	 2u

// Group size for dispatching based on a groom vertex/curve/cluster count. 
// This defines ensures the group size is consistent across the hair pipeline, 
// and ensures dispatch count are smaller than 65k limits
#define HAIR_VERTEXCOUNT_GROUP_SIZE  1024u
#define HAIR_CURVECOUNT_GROUP_SIZE   1024u
#define HAIR_CLUSTERCOUNT_GROUP_SIZE 1024u

#define MAX_HAIR_MACROGROUP_COUNT 16

// HAIR_ATTRIBUTE_MAX rounded to 4
#define HAIR_ATTRIBUTE_OFFSET_COUNT ((HAIR_ATTRIBUTE_COUNT + 3) / 4)

// Pack all offset into uint4
#define PACK_HAIR_ATTRIBUTE_OFFSETS(Out, In) \
	for (uint32 AttributeIt4 = 0; AttributeIt4 < HAIR_ATTRIBUTE_OFFSET_COUNT; ++AttributeIt4) \
	{ \
		Out[AttributeIt4] = FUintVector4(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF); \
	} \
	for (uint32 AttributeIt = 0; AttributeIt < HAIR_ATTRIBUTE_COUNT; ++AttributeIt) \
	{ \
		const uint32 Index4 = AttributeIt & (~0x3); \
		const uint32 SubIndex = AttributeIt - Index4; \
		const uint32 IndexDiv4 = Index4 >> 2u; \
		Out[IndexDiv4][SubIndex] = In[AttributeIt]; \
	}