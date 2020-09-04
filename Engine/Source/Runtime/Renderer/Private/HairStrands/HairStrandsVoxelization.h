// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HairStrandsVoxelization.h: Hair voxelization implementation.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "RenderGraphResources.h"
#include "Renderer/Private/SceneRendering.h"

struct FVirtualVoxelNodeDesc
{
	FVector WorldMinAABB = FVector::ZeroVector;
	FVector WorldMaxAABB = FVector::ZeroVector;
	FIntVector PageIndexResolution = FIntVector::ZeroValue;
	FMatrix WorldToClip;
};

struct FPackedVirtualVoxelNodeDesc
{
	// This is just a placeholder having the correct size. The actual definition is in HairStradsNVoxelPageCommon.ush
	const static EPixelFormat Format = PF_R32G32B32A32_UINT;
	const static uint32 ComponentCount = 2;

	// Shader View is struct { uint4; uint4; }
	FVector	MinAABB;
	uint32	PackedPageIndexResolution;
	FVector	MaxAABB;
	uint32	PageIndexOffset;
};

// PixelRadiusAtDepth1 shouldn't be stored into this structure should be view independent, 
// but is put here for convenience at the moment since multiple views are not supported 
// at the moment
BEGIN_SHADER_PARAMETER_STRUCT(FVirtualVoxelCommonParameters, )
	SHADER_PARAMETER(FIntVector, PageCountResolution)
	SHADER_PARAMETER(float, VoxelWorldSize)
	SHADER_PARAMETER(FIntVector, PageTextureResolution)
	SHADER_PARAMETER(uint32, PageCount)
	SHADER_PARAMETER(uint32, PageResolution)
	SHADER_PARAMETER(uint32, PageIndexCount)
	SHADER_PARAMETER(uint32, IndirectDispatchGroupSize)
	SHADER_PARAMETER(uint32, NodeDescCount)
	SHADER_PARAMETER(float, DensityScale)
	SHADER_PARAMETER(float, DepthBiasScale_Light)
	SHADER_PARAMETER(float, DepthBiasScale_Environment)
	SHADER_PARAMETER(float, SteppingScale)
	SHADER_PARAMETER(float, HairCoveragePixelRadiusAtDepth1) 
	SHADER_PARAMETER_SRV(Buffer<uint>, PageIndexBuffer)
	SHADER_PARAMETER_SRV(Buffer<uint>, PageIndexCoordBuffer)
	SHADER_PARAMETER_SRV(StructuredBuffer<FPackedVirtualVoxelNodeDesc>, NodeDescBuffer) // Packed into 2 x uint4
	SHADER_PARAMETER_TEXTURE(Texture2D<float>, HairCoverageLUT)
	SHADER_PARAMETER_SAMPLER(SamplerState, HairCoverageSampler)
END_SHADER_PARAMETER_STRUCT()

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FVirtualVoxelParameters, RENDERER_API)
	SHADER_PARAMETER_STRUCT_INCLUDE(FVirtualVoxelCommonParameters, Common)
	SHADER_PARAMETER_TEXTURE(Texture3D<uint>, PageTexture)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

struct FVirtualVoxelResources
{
	FVirtualVoxelParameters	Parameters;
	TUniformBufferRef<FVirtualVoxelParameters> UniformBuffer;

	TRefCountPtr<IPooledRenderTarget>	PageTexture;

	TRefCountPtr<FRDGPooledBuffer>		PageIndexBuffer;
	FShaderResourceViewRHIRef			PageIndexBufferSRV = nullptr;

	TRefCountPtr<FRDGPooledBuffer>		NodeDescBuffer;
	FShaderResourceViewRHIRef			NodeDescBufferSRV = nullptr;

	TRefCountPtr<FRDGPooledBuffer>		PageIndexCoordBuffer;
	FShaderResourceViewRHIRef			PageIndexCoordBufferSRV = nullptr;

	TRefCountPtr<FRDGPooledBuffer>		IndirectArgsBuffer;

	TRefCountPtr<FRDGPooledBuffer>		PageIndexGlobalCounter;

	TRefCountPtr<FRDGPooledBuffer>		VoxelizationViewInfoBuffer;

	const bool IsValid() const { return UniformBuffer.IsValid(); }
};

/// Global enable/disable for hair voxelization
bool IsHairStrandsVoxelizationEnable();
bool IsHairStrandsForVoxelTransmittanceAndShadowEnable();

void VoxelizeHairStrands(
	FRDGBuilder& GraphBuilder,
	const class FScene* Scene,
	const TArray<FViewInfo>& Views,
	struct FHairStrandsMacroGroupViews& MacroGroupViews);


struct FHairStrandsOcclusionResources
{
	FRDGTextureRef CategorizationTexture = nullptr;
	FRDGTextureRef LightChannelMaskTexture = nullptr;
	const FVirtualVoxelResources* VoxelResources = nullptr;
	bool bUseHairVoxel = false;
};