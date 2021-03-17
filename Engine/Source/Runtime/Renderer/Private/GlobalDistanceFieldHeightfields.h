// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GlobalDistanceFieldHeightfields.h
=============================================================================*/

#pragma once

bool ShouldCompileGlobalDistanceFieldShader(const FGlobalShaderPermutationParameters& Parameters)
{
	return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportDistanceFieldAO(Parameters.Platform) && IsUsingDistanceFields(Parameters.Platform);
}

class FMarkHeightfieldPagesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMarkHeightfieldPagesCS);
	SHADER_USE_PARAMETER_STRUCT(FMarkHeightfieldPagesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWMarkedHeightfieldPageBuffer)
		RDG_BUFFER_ACCESS(PageUpdateIndirectArgBuffer, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, PageUpdateTileBuffer)
		SHADER_PARAMETER(FVector, PageCoordToPageWorldCenterScale)
		SHADER_PARAMETER(FVector, PageCoordToPageWorldCenterBias)
		SHADER_PARAMETER(FVector, PageWorldExtent)
		SHADER_PARAMETER(FVector, InvPageGridResolution)
		SHADER_PARAMETER(FIntVector, PageGridResolution)
		SHADER_PARAMETER(float, ClipmapVoxelExtent)
		SHADER_PARAMETER(float, InfluenceRadius)
		SHADER_PARAMETER_TEXTURE(Texture2D, HeightfieldTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, HeightfieldSampler)
		SHADER_PARAMETER_TEXTURE(Texture2D, VisibilityTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, VisibilitySampler)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, HeightfieldDescriptions)
		SHADER_PARAMETER(uint32, NumHeightfields)
		SHADER_PARAMETER(float, HeightfieldThickness)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileGlobalDistanceFieldShader(Parameters);
	}

	static FIntVector GetGroupSize()
	{
		return FIntVector(16, 16, 1);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_X"), GetGroupSize().X);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_Y"), GetGroupSize().Y);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_Z"), GetGroupSize().Z);
	}												   
};

IMPLEMENT_GLOBAL_SHADER(FMarkHeightfieldPagesCS, "/Engine/Private/GlobalDistanceFieldHeightfields.usf", "MarkHeightfieldPagesCS", SF_Compute);

class FBuildHeightfieldComposeTilesIndirectArgBufferCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FBuildHeightfieldComposeTilesIndirectArgBufferCS);
	SHADER_USE_PARAMETER_STRUCT(FBuildHeightfieldComposeTilesIndirectArgBufferCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWBuildHeightfieldComposeTilesIndirectArgBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWPageComposeHeightfieldIndirectArgBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, PageUpdateIndirectArgBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileGlobalDistanceFieldShader(Parameters);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_X"), 1);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_Y"), 1);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_Z"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FBuildHeightfieldComposeTilesIndirectArgBufferCS, "/Engine/Private/GlobalDistanceFieldHeightfields.usf", "BuildHeightfieldComposeTilesIndirectArgBufferCS", SF_Compute);

class FBuildHeightfieldComposeTilesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FBuildHeightfieldComposeTilesCS);
	SHADER_USE_PARAMETER_STRUCT(FBuildHeightfieldComposeTilesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWPageComposeHeightfieldIndirectArgBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWPageComposeHeightfieldTileBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, PageUpdateTileBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, MarkedHeightfieldPageBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, PageUpdateIndirectArgBuffer)
		RDG_BUFFER_ACCESS(BuildHeightfieldComposeTilesIndirectArgBuffer, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileGlobalDistanceFieldShader(Parameters);
	}

	static FIntVector GetGroupSize()
	{
		return FIntVector(64, 1, 1);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_X"), GetGroupSize().X);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_Y"), GetGroupSize().Y);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_Z"), GetGroupSize().Z);
	}												   
};

IMPLEMENT_GLOBAL_SHADER(FBuildHeightfieldComposeTilesCS, "/Engine/Private/GlobalDistanceFieldHeightfields.usf", "BuildHeightfieldComposeTilesCS", SF_Compute);

class FComposeHeightfieldsIntoPagesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FComposeHeightfieldsIntoPagesCS);
	SHADER_USE_PARAMETER_STRUCT(FComposeHeightfieldsIntoPagesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, RWPageAtlasTexture)
		RDG_BUFFER_ACCESS(ComposeIndirectArgBuffer, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, ComposeTileBuffer)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<uint>, PageTableLayerTexture)
		SHADER_PARAMETER(FVector, InvPageGridResolution)
		SHADER_PARAMETER(FIntVector, PageGridResolution)
		SHADER_PARAMETER(FVector, PageCoordToVoxelCenterScale)
		SHADER_PARAMETER(FVector, PageCoordToVoxelCenterBias)
		SHADER_PARAMETER(FVector, PageCoordToPageWorldCenterScale)
		SHADER_PARAMETER(FVector, PageCoordToPageWorldCenterBias)
		SHADER_PARAMETER(FVector4, ClipmapVolumeWorldToUVAddAndMul)
		SHADER_PARAMETER(float, ClipmapVoxelExtent)
		SHADER_PARAMETER(float, InfluenceRadius)
		SHADER_PARAMETER(uint32, PageTableClipmapOffsetZ)
		SHADER_PARAMETER_TEXTURE(Texture2D, HeightfieldTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, HeightfieldSampler)
		SHADER_PARAMETER_TEXTURE(Texture2D, VisibilityTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, VisibilitySampler)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, HeightfieldDescriptions)
		SHADER_PARAMETER(uint32, NumHeightfields)
		SHADER_PARAMETER(float, HeightfieldThickness)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileGlobalDistanceFieldShader(Parameters);
	}

	static FIntVector GetGroupSize()
	{
		return FIntVector(8, 8, 1);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_X"), GetGroupSize().X);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_Y"), GetGroupSize().Y);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_Z"), GetGroupSize().Z);
	}												   
};

IMPLEMENT_GLOBAL_SHADER(FComposeHeightfieldsIntoPagesCS, "/Engine/Private/GlobalDistanceFieldHeightfields.usf", "ComposeHeightfieldsIntoPagesCS", SF_Compute);

extern FRDGBufferRef UploadHeightfieldDescriptions(FRDGBuilder& GraphBuilder, const TArray<FHeightfieldComponentDescription>& HeightfieldDescriptions, FVector2D InvLightingAtlasSize, float InvDownsampleFactor);