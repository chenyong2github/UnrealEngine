// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "HairStrandsInterface.h"
#include "Shader.h"

/* Project hair strands onto a LOD mesh */
void ProjectHairStrandsOntoMesh(
	FRHICommandListImmediate& RHICmdList, 
	TShaderMap<FGlobalShaderType>* ShaderMap,
	const int32 LODIndex,
	const FHairStrandsProjectionMeshData& ProjectionMeshData, 
	FHairStrandsProjectionHairData::HairGroup& ProjectionHairData);

enum class HairStrandsTriangleType
{
	RestPose,
	DeformedPose,
};

/* Update the triangles information on which hair stands have been projected */
void UpdateHairStrandsMeshTriangles(
	FRHICommandListImmediate& RHICmdList,
	TShaderMap<FGlobalShaderType>* ShaderMap,
	const int32 LODIndex,
	const HairStrandsTriangleType Type,
	const FHairStrandsProjectionMeshData::LOD& ProjectionMeshData,
	FHairStrandsProjectionHairData::HairGroup& ProjectionHairData);