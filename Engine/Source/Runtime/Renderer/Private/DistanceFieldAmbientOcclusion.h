// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	DistanceFieldAmbientOcclusion.h
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RenderResource.h"
#include "ShaderParameters.h"
#include "UniformBuffer.h"
#include "RHIStaticStates.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "PostProcess/SceneRenderTargets.h"
#include "ScenePrivate.h"

const static int32 GAOMaxSupportedLevel = 6;
/** Number of cone traced directions. */
const int32 NumConeSampleDirections = 9;

/** Base downsample factor that all distance field AO operations are done at. */
const int32 GAODownsampleFactor = 2;

extern const uint32 UpdateObjectsGroupSize;

extern FIntPoint GetBufferSizeForAO();

class FDistanceFieldAOParameters
{
public:
	float GlobalMaxOcclusionDistance;
	float ObjectMaxOcclusionDistance;
	float Contrast;

	FDistanceFieldAOParameters(float InOcclusionMaxDistance, float InContrast = 0);
};

BEGIN_SHADER_PARAMETER_STRUCT(FTileIntersectionParameters, )
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FVector4>, RWTileConeAxisAndCos)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FVector4>, RWTileConeDepthRanges)

	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWNumCulledTilesArray)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWCulledTilesStartOffsetArray)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCulledTileDataArray)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWObjectTilesIndirectArguments)

	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector4>, TileConeAxisAndCos)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector4>, TileConeDepthRanges)

	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, NumCulledTilesArray)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CulledTilesStartOffsetArray)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CulledTileDataArray)

	SHADER_PARAMETER(FIntPoint, TileListGroupSize)
END_SHADER_PARAMETER_STRUCT()

static int32 CulledTileDataStride = 2;
static int32 ConeTraceObjectsThreadGroupSize = 64;

inline void TileIntersectionModifyCompilationEnvironment(EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment)
{
	OutEnvironment.SetDefine(TEXT("CULLED_TILE_DATA_STRIDE"), CulledTileDataStride);
	extern int32 GDistanceFieldAOTileSizeX;
	OutEnvironment.SetDefine(TEXT("CULLED_TILE_SIZEX"), GDistanceFieldAOTileSizeX);
	extern int32 GConeTraceDownsampleFactor;
	OutEnvironment.SetDefine(TEXT("TRACE_DOWNSAMPLE_FACTOR"), GConeTraceDownsampleFactor);
	OutEnvironment.SetDefine(TEXT("CONE_TRACE_OBJECTS_THREADGROUP_SIZE"), ConeTraceObjectsThreadGroupSize);
}

BEGIN_SHADER_PARAMETER_STRUCT(FAOScreenGridParameters, )
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWScreenGridConeVisibility)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ScreenGridConeVisibility)
	SHADER_PARAMETER(FIntPoint, ScreenGridConeVisibilitySize)
END_SHADER_PARAMETER_STRUCT()

extern void GetSpacedVectors(uint32 FrameNumber, TArray<FVector, TInlineAllocator<9> >& OutVectors);

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FAOSampleData2,)
	SHADER_PARAMETER_ARRAY(FVector4,SampleDirections,[NumConeSampleDirections])
END_GLOBAL_SHADER_PARAMETER_STRUCT()

inline float GetMaxAOViewDistance()
{
	extern float GAOMaxViewDistance;
	// Scene depth stored in fp16 alpha, must fade out before it runs out of range
	// The fade extends past GAOMaxViewDistance a bit
	return FMath::Min(GAOMaxViewDistance, 65000.0f);
}

class FAOParameters
{
	DECLARE_TYPE_LAYOUT(FAOParameters, NonVirtual);
public:
	void Bind(const FShaderParameterMap& ParameterMap)
	{
		AOObjectMaxDistance.Bind(ParameterMap,TEXT("AOObjectMaxDistance"));
		AOStepScale.Bind(ParameterMap,TEXT("AOStepScale"));
		AOStepExponentScale.Bind(ParameterMap,TEXT("AOStepExponentScale"));
		AOMaxViewDistance.Bind(ParameterMap,TEXT("AOMaxViewDistance"));
		AOGlobalMaxOcclusionDistance.Bind(ParameterMap,TEXT("AOGlobalMaxOcclusionDistance"));
	}

	friend FArchive& operator<<(FArchive& Ar,FAOParameters& Parameters)
	{
		Ar << Parameters.AOObjectMaxDistance;
		Ar << Parameters.AOStepScale;
		Ar << Parameters.AOStepExponentScale;
		Ar << Parameters.AOMaxViewDistance;
		Ar << Parameters.AOGlobalMaxOcclusionDistance;
		return Ar;
	}

	template<typename ShaderRHIParamRef>
	void Set(FRHICommandList& RHICmdList, const ShaderRHIParamRef ShaderRHI, const FDistanceFieldAOParameters& Parameters)
	{
		SetShaderValue(RHICmdList, ShaderRHI, AOObjectMaxDistance, Parameters.ObjectMaxOcclusionDistance);

		extern float GAOConeHalfAngle;
		const float AOLargestSampleOffset = Parameters.ObjectMaxOcclusionDistance / (1 + FMath::Tan(GAOConeHalfAngle));

		extern float GAOStepExponentScale;
		extern uint32 GAONumConeSteps;
		float AOStepScaleValue = AOLargestSampleOffset / FMath::Pow(2.0f, GAOStepExponentScale * (GAONumConeSteps - 1));
		SetShaderValue(RHICmdList, ShaderRHI, AOStepScale, AOStepScaleValue);

		SetShaderValue(RHICmdList, ShaderRHI, AOStepExponentScale, GAOStepExponentScale);

		SetShaderValue(RHICmdList, ShaderRHI, AOMaxViewDistance, GetMaxAOViewDistance());

		const float GlobalMaxOcclusionDistance = Parameters.GlobalMaxOcclusionDistance;
		SetShaderValue(RHICmdList, ShaderRHI, AOGlobalMaxOcclusionDistance, GlobalMaxOcclusionDistance);
	}

private:
	
		LAYOUT_FIELD(FShaderParameter, AOObjectMaxDistance)
		LAYOUT_FIELD(FShaderParameter, AOStepScale)
		LAYOUT_FIELD(FShaderParameter, AOStepExponentScale)
		LAYOUT_FIELD(FShaderParameter, AOMaxViewDistance)
		LAYOUT_FIELD(FShaderParameter, AOGlobalMaxOcclusionDistance)
	
};

class FDFAOUpsampleParameters
{
	DECLARE_TYPE_LAYOUT(FDFAOUpsampleParameters, NonVirtual);
public:
	void Bind(const FShaderParameterMap& ParameterMap)
	{
		BentNormalAOTexture.Bind(ParameterMap, TEXT("BentNormalAOTexture"));
		BentNormalAOSampler.Bind(ParameterMap, TEXT("BentNormalAOSampler"));
		AOBufferBilinearUVMax.Bind(ParameterMap, TEXT("AOBufferBilinearUVMax"));
		DistanceFadeScale.Bind(ParameterMap, TEXT("DistanceFadeScale"));
		AOMaxViewDistance.Bind(ParameterMap, TEXT("AOMaxViewDistance"));
	}

	void Set(FRHICommandList& RHICmdList, FRHIPixelShader* ShaderRHI, const FViewInfo& View, FRHITexture* DistanceFieldAOBentNormal)
	{
		FRHITexture* BentNormalAO = DistanceFieldAOBentNormal ? DistanceFieldAOBentNormal : (FRHITexture*)GWhiteTexture->TextureRHI;
		SetTextureParameter(RHICmdList, ShaderRHI, BentNormalAOTexture, BentNormalAOSampler, TStaticSamplerState<SF_Bilinear>::GetRHI(), BentNormalAO);

		FIntPoint const AOBufferSize = GetBufferSizeForAO();
		FVector2D const UVMax(
			(View.ViewRect.Width() / GAODownsampleFactor - 0.51f) / AOBufferSize.X, // 0.51 - so bilateral gather4 won't sample invalid texels
			(View.ViewRect.Height() / GAODownsampleFactor - 0.51f) / AOBufferSize.Y);
		SetShaderValue(RHICmdList, ShaderRHI, AOBufferBilinearUVMax, UVMax);

		SetShaderValue(RHICmdList, ShaderRHI, AOMaxViewDistance, GetMaxAOViewDistance());

		extern float GAOViewFadeDistanceScale;
		const float DistanceFadeScaleValue = 1.0f / ((1.0f - GAOViewFadeDistanceScale) * GetMaxAOViewDistance());
		SetShaderValue(RHICmdList, ShaderRHI, DistanceFadeScale, DistanceFadeScaleValue);
	}

	/** Serializer. */
	friend FArchive& operator<<(FArchive& Ar, FDFAOUpsampleParameters& P)
	{
		Ar << P.BentNormalAOTexture;
		Ar << P.BentNormalAOSampler;
		Ar << P.AOBufferBilinearUVMax;
		Ar << P.DistanceFadeScale;
		Ar << P.AOMaxViewDistance;

		return Ar;
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, BentNormalAOTexture);
	LAYOUT_FIELD(FShaderResourceParameter, BentNormalAOSampler);
	LAYOUT_FIELD(FShaderParameter, AOBufferBilinearUVMax);
	LAYOUT_FIELD(FShaderParameter, DistanceFadeScale);
	LAYOUT_FIELD(FShaderParameter, AOMaxViewDistance);
};

class FMaxSizedRWBuffers : public FRenderResource
{
public:
	FMaxSizedRWBuffers()
	{
		MaxSize = 0;
	}

	virtual void InitDynamicRHI()
	{
		check(0);
	}

	virtual void ReleaseDynamicRHI()
	{
		check(0);
	}

	void AllocateFor(int32 InMaxSize)
	{
		bool bReallocate = false;

		if (InMaxSize > MaxSize)
		{
			MaxSize = InMaxSize;
			bReallocate = true;
		}

		if (!IsInitialized())
		{
			InitResource();
		}
		else if (bReallocate)
		{
			UpdateRHI();
		}
	}

	int32 GetMaxSize() const { return MaxSize; }

protected:
	int32 MaxSize;
};

class FScreenGridParameters
{
	DECLARE_TYPE_LAYOUT(FScreenGridParameters, NonVirtual);
public:
	void Bind(const FShaderParameterMap& ParameterMap)
	{
		BaseLevelTexelSize.Bind(ParameterMap, TEXT("BaseLevelTexelSize"));
		JitterOffset.Bind(ParameterMap, TEXT("JitterOffset"));
		DistanceFieldNormalTexture.Bind(ParameterMap, TEXT("DistanceFieldNormalTexture"));
		DistanceFieldNormalSampler.Bind(ParameterMap, TEXT("DistanceFieldNormalSampler"));
	}

	template<typename TParamRef>
	void Set(FRHICommandList& RHICmdList, const TParamRef& ShaderRHI, const FViewInfo& View, FRHITexture* DistanceFieldNormal)
	{
		const FIntPoint DownsampledBufferSize = GetBufferSizeForAO();
		const FVector2D BaseLevelTexelSizeValue(1.0f / DownsampledBufferSize.X, 1.0f / DownsampledBufferSize.Y);
		SetShaderValue(RHICmdList, ShaderRHI, BaseLevelTexelSize, BaseLevelTexelSizeValue);

		extern FVector2D GetJitterOffset(int32 SampleIndex);
		SetShaderValue(RHICmdList, ShaderRHI, JitterOffset, GetJitterOffset(View.GetDistanceFieldTemporalSampleIndex()));


		SetTextureParameter(
			RHICmdList,
			ShaderRHI,
			DistanceFieldNormalTexture,
			DistanceFieldNormalSampler,
			TStaticSamplerState<SF_Point,AM_Wrap,AM_Wrap,AM_Wrap>::GetRHI(),
			DistanceFieldNormal
			);
	}

	friend FArchive& operator<<(FArchive& Ar,FScreenGridParameters& P)
	{
		Ar << P.BaseLevelTexelSize << P.JitterOffset << P.DistanceFieldNormalTexture << P.DistanceFieldNormalSampler;
		return Ar;
	}

private:
	
		LAYOUT_FIELD(FShaderParameter, BaseLevelTexelSize)
		LAYOUT_FIELD(FShaderParameter, JitterOffset)
		LAYOUT_FIELD(FShaderResourceParameter, DistanceFieldNormalTexture)
		LAYOUT_FIELD(FShaderResourceParameter, DistanceFieldNormalSampler)
	
};

extern void TrackGPUProgress(FRHICommandListImmediate& RHICmdList, uint32 DebugId);

extern bool ShouldRenderDeferredDynamicSkyLight(const FScene* Scene, const FSceneViewFamily& ViewFamily);

class FDistanceFieldCulledObjectBufferParameters;

extern void CullObjectsToView(FRDGBuilder& GraphBuilder, FScene* Scene, const FViewInfo& View, const FDistanceFieldAOParameters& Parameters, FDistanceFieldCulledObjectBufferParameters& CulledObjectBuffers);
extern void BuildTileObjectLists(FRDGBuilder& GraphBuilder,
	FScene* Scene,
	TArray<FViewInfo>& Views,
	FRDGBufferRef ObjectIndirectArguments,
	const FDistanceFieldCulledObjectBufferParameters& CulledObjectBufferParameters,
	FTileIntersectionParameters TileIntersectionParameters,
	FRDGTextureRef DistanceFieldNormal,
	const FDistanceFieldAOParameters& Parameters);
extern FIntPoint GetTileListGroupSizeForView(const FViewInfo& View);
