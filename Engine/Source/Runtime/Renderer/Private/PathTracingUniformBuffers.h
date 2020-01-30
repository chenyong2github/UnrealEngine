// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PathTracingUniformBuffers.h
=============================================================================*/

#pragma once

#include "UniformBuffer.h"
#include "RayTracingDefinitions.h"

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FPathTracingData, )
	SHADER_PARAMETER(uint32, MaxBounces)
	SHADER_PARAMETER(FIntVector, TileOffset)
END_GLOBAL_SHADER_PARAMETER_STRUCT()


// Lights

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FPathTracingLightData, )
	SHADER_PARAMETER(uint32, Count)
	SHADER_PARAMETER_ARRAY(uint32, Type, [RAY_TRACING_LIGHT_COUNT_MAXIMUM])
	// Geometry
	SHADER_PARAMETER_ARRAY(FVector, Position, [RAY_TRACING_LIGHT_COUNT_MAXIMUM])
	SHADER_PARAMETER_ARRAY(FVector, Normal, [RAY_TRACING_LIGHT_COUNT_MAXIMUM])
	SHADER_PARAMETER_ARRAY(FVector, dPdu, [RAY_TRACING_LIGHT_COUNT_MAXIMUM])
	SHADER_PARAMETER_ARRAY(FVector, dPdv, [RAY_TRACING_LIGHT_COUNT_MAXIMUM])
	// Color
	SHADER_PARAMETER_ARRAY(FVector, Color, [RAY_TRACING_LIGHT_COUNT_MAXIMUM])
	// Light-specific
	SHADER_PARAMETER_ARRAY(FVector, Dimensions, [RAY_TRACING_LIGHT_COUNT_MAXIMUM])
	SHADER_PARAMETER_ARRAY(float, Attenuation, [RAY_TRACING_LIGHT_COUNT_MAXIMUM])
	SHADER_PARAMETER_ARRAY(float, RectLightBarnCosAngle, [RAY_TRACING_LIGHT_COUNT_MAXIMUM])
	SHADER_PARAMETER_ARRAY(float, RectLightBarnLength, [RAY_TRACING_LIGHT_COUNT_MAXIMUM])
END_GLOBAL_SHADER_PARAMETER_STRUCT()


BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FPathTracingAdaptiveSamplingData, )
	SHADER_PARAMETER(uint32, UseAdaptiveSampling)
	SHADER_PARAMETER(uint32, RandomSequence)
	SHADER_PARAMETER(uint32, MinimumSamplesPerPixel)
	SHADER_PARAMETER(uint32, Iteration)
	SHADER_PARAMETER(uint32, TemporalSeed)
	SHADER_PARAMETER(float, MaxNormalBias)
	SHADER_PARAMETER(FIntVector, VarianceDimensions)
	SHADER_PARAMETER_SRV(Buffer<float>, VarianceMipTree)
END_GLOBAL_SHADER_PARAMETER_STRUCT()
