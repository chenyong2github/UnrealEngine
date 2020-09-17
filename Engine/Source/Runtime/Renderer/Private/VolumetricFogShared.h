// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHIDefinitions.h"
#include "SceneView.h"
#include "SceneRendering.h"

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FVoxelizeVolumePassUniformParameters, )
	SHADER_PARAMETER_STRUCT(FSceneTextureUniformParameters, SceneTextures)
	SHADER_PARAMETER(FMatrix, ViewToVolumeClip)
	SHADER_PARAMETER(FVector4, FrameJitterOffset0)
	SHADER_PARAMETER_STRUCT(FVolumetricFogGlobalData, VolumetricFog)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

extern FVector VolumetricFogTemporalRandom(uint32 FrameNumber);

struct FVolumetricFogIntegrationParameterData
{
	FVolumetricFogIntegrationParameterData() :
		bTemporalHistoryIsValid(false)
	{}

	bool bTemporalHistoryIsValid;
	TArray<FVector4, TInlineAllocator<16>> FrameJitterOffsetValues;
	FRDGTexture* VBufferA;
	FRDGTexture* VBufferB;
	FRDGTextureUAV* VBufferA_UAV;
	FRDGTextureUAV* VBufferB_UAV;

	FRDGTexture* LightScattering;
	FRDGTextureUAV* LightScatteringUAV;
};

class FVolumetricFogIntegrationParameters
{
	DECLARE_TYPE_LAYOUT(FVolumetricFogIntegrationParameters, NonVirtual);
public:

	void Bind(const FShaderParameterMap& ParameterMap)
	{
		VBufferA.Bind(ParameterMap, TEXT("VBufferA"));
		VBufferB.Bind(ParameterMap, TEXT("VBufferB"));
		LightScattering.Bind(ParameterMap, TEXT("LightScattering"));
		IntegratedLightScattering.Bind(ParameterMap, TEXT("IntegratedLightScattering"));
		IntegratedLightScatteringSampler.Bind(ParameterMap, TEXT("IntegratedLightScatteringSampler"));
		VolumetricFogData.Bind(ParameterMap, TEXT("VolumetricFog"));
		UnjitteredClipToTranslatedWorld.Bind(ParameterMap, TEXT("UnjitteredClipToTranslatedWorld")); 
		UnjitteredPrevWorldToClip.Bind(ParameterMap, TEXT("UnjitteredPrevWorldToClip")); 
		FrameJitterOffsets.Bind(ParameterMap, TEXT("FrameJitterOffsets")); 
		HistoryWeight.Bind(ParameterMap, TEXT("HistoryWeight")); 
		HistoryMissSuperSampleCount.Bind(ParameterMap, TEXT("HistoryMissSuperSampleCount")); 
	}

	template<typename ShaderRHIParamRef>
	void Set(
		FRHICommandList& RHICmdList, 
		const ShaderRHIParamRef& ShaderRHI, 
		const FViewInfo& View, 
		const FVolumetricFogIntegrationParameterData& IntegrationData) const
	{
		SetSamplerParameter(RHICmdList, ShaderRHI, IntegratedLightScatteringSampler, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
		
		if (VolumetricFogData.IsBound())
		{
			SetUniformBufferParameter(RHICmdList, ShaderRHI, VolumetricFogData, View.VolumetricFogResources.VolumetricFogGlobalData);
		}

		if (UnjitteredClipToTranslatedWorld.IsBound())
		{
			FMatrix UnjitteredInvTranslatedViewProjectionMatrix = View.ViewMatrices.ComputeInvProjectionNoAAMatrix() * View.ViewMatrices.GetTranslatedViewMatrix().GetTransposed();
			SetShaderValue(RHICmdList, ShaderRHI, UnjitteredClipToTranslatedWorld, UnjitteredInvTranslatedViewProjectionMatrix);
		}

		if (UnjitteredPrevWorldToClip.IsBound())
		{
			FMatrix UnjitteredViewProjectionMatrix = View.PrevViewInfo.ViewMatrices.GetViewMatrix() * View.PrevViewInfo.ViewMatrices.ComputeProjectionNoAAMatrix();
			SetShaderValue(RHICmdList, ShaderRHI, UnjitteredPrevWorldToClip, UnjitteredViewProjectionMatrix);
		}

		if (FrameJitterOffsets.IsBound())
		{
			SetShaderValueArray(RHICmdList, ShaderRHI, FrameJitterOffsets, IntegrationData.FrameJitterOffsetValues.GetData(), IntegrationData.FrameJitterOffsetValues.Num());
		}

		extern float GVolumetricFogHistoryWeight;
		SetShaderValue(RHICmdList, ShaderRHI, HistoryWeight, IntegrationData.bTemporalHistoryIsValid ? GVolumetricFogHistoryWeight : 0.0f);

		extern int32 GVolumetricFogHistoryMissSupersampleCount;
		SetShaderValue(RHICmdList, ShaderRHI, HistoryMissSuperSampleCount, FMath::Clamp(GVolumetricFogHistoryMissSupersampleCount, 1, 16));
	}

	/** Serializer. */
	friend FArchive& operator<<(FArchive& Ar,FVolumetricFogIntegrationParameters& P)
	{
		Ar << P.VBufferA;
		Ar << P.VBufferB;
		Ar << P.LightScattering;
		Ar << P.IntegratedLightScattering;
		Ar << P.IntegratedLightScatteringSampler;
		Ar << P.VolumetricFogData;
		Ar << P.UnjitteredClipToTranslatedWorld;
		Ar << P.UnjitteredPrevWorldToClip;
		Ar << P.FrameJitterOffsets;
		Ar << P.HistoryWeight;
		Ar << P.HistoryMissSuperSampleCount;
		return Ar;
	}

private:
	LAYOUT_FIELD(FRWShaderParameter, VBufferA)
	LAYOUT_FIELD(FRWShaderParameter, VBufferB)
	LAYOUT_FIELD(FRWShaderParameter, LightScattering)
	LAYOUT_FIELD(FRWShaderParameter, IntegratedLightScattering)
	LAYOUT_FIELD(FShaderResourceParameter, IntegratedLightScatteringSampler)
	LAYOUT_FIELD(FShaderUniformBufferParameter, VolumetricFogData)
	LAYOUT_FIELD(FShaderParameter, UnjitteredClipToTranslatedWorld)
	LAYOUT_FIELD(FShaderParameter, UnjitteredPrevWorldToClip)
	LAYOUT_FIELD(FShaderParameter, FrameJitterOffsets)
	LAYOUT_FIELD(FShaderParameter, HistoryWeight)
	LAYOUT_FIELD(FShaderParameter, HistoryMissSuperSampleCount)
};

inline int32 ComputeZSliceFromDepth(float SceneDepth, FVector GridZParams)
{
	return FMath::TruncToInt(FMath::Log2(SceneDepth * GridZParams.X + GridZParams.Y) * GridZParams.Z);
}

extern FVector GetVolumetricFogGridZParams(float NearPlane, float FarPlane, int32 GridSizeZ);