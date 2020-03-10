// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "HairStrandsInterface.h"
#include "RenderGraphResources.h"
#include "Shader.h"

/* Project hair strands onto a LOD mesh */
void ProjectHairStrandsOntoMesh(
	class FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const int32 LODIndex,
	const FHairStrandsProjectionMeshData& ProjectionMeshData,
	FHairStrandsProjectionHairData::HairGroup& ProjectionHairData, 
	FBufferTransitionQueue& OutTransitionQueue);

void TransferMesh(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const int32 LODIndex,
	const FHairStrandsProjectionMeshData& SourceMeshData,
	const FHairStrandsProjectionMeshData& TargetMeshData,
	struct FRWBuffer& OutPositionBuffer,
	FBufferTransitionQueue& OutTransitionQueue);

enum class HairStrandsTriangleType
{
	RestPose,
	DeformedPose,
};

/* Update the triangles information on which hair stands have been projected */
void UpdateHairStrandsMeshTriangles(
	class FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const int32 LODIndex,
	const HairStrandsTriangleType Type,
	const FHairStrandsProjectionMeshData::LOD& ProjectionMeshData,
	FHairStrandsProjectionHairData::HairGroup& ProjectionHairData,
	FBufferTransitionQueue& TransitionQueue);

/* Init the samples information to be used for interpolation*/
void InitHairStrandsMeshSamples(
	class FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const int32 LODIndex,
	const HairStrandsTriangleType Type,
	const FHairStrandsProjectionMeshData::LOD& ProjectionMeshData,
	FHairStrandsProjectionHairData::HairGroup& ProjectionHairData,
	FBufferTransitionQueue& TransitionQueue);

/* Update the samples information to be used for interpolation*/
void UpdateHairStrandsMeshSamples(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const int32 LODIndex,
	const FHairStrandsProjectionMeshData::LOD& ProjectionMeshData,
	FHairStrandsProjectionHairData::HairGroup& ProjectionHairData,
	FBufferTransitionQueue& TransitionQueue);

void GenerateFolliculeMask(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const FIntPoint Resolution,
	const uint32 MipCount,
	const uint32 KernelSizeInPixels,
	const uint32 Channel,
	const int32 LODIndex,
	const FHairStrandsProjectionHairData& HairData,
	FRDGTextureRef& OutTexture);

void AddComputeMipsPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	FRDGTextureRef& OutTexture);

void InterpolateHairStrandsMeshTriangles(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const int32 LODIndex,
	const FHairStrandsProjectionMeshData::LOD& ProjectionMeshData,
	FHairStrandsProjectionHairData::HairGroup& ProjectionHairData,
	FBufferTransitionQueue& TransitionQueue);

