// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GlobalDistanceFieldParameters.h
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "ShaderParameters.h"
#include "RenderUtils.h"
#include "RHIStaticStates.h"

class FShaderParameterMap;

/** Must match global distance field shaders. */
const int32 GMaxGlobalDistanceFieldClipmaps = 5;

class FGlobalDistanceFieldParameterData
{
public:

	FGlobalDistanceFieldParameterData()
	{
		FPlatformMemory::Memzero(this, sizeof(FGlobalDistanceFieldParameterData));
	}

	FVector4 CenterAndExtent[GMaxGlobalDistanceFieldClipmaps];
	FVector4 WorldToUVAddAndMul[GMaxGlobalDistanceFieldClipmaps];
	FVector PageTableScrollOffset[GMaxGlobalDistanceFieldClipmaps];
	FRHITexture* PageAtlasTexture;
	FRHITexture* PageTableTexture;
	int32 ClipmapSizeInPages;
	FVector InvPageAtlasSize;
	int32 MaxPageNum;
	float GlobalDFResolution;
	float MaxDFAOConeDistance;
	int32 NumGlobalSDFClipmaps;
};

class FGlobalDistanceFieldParameters
{
	DECLARE_INLINE_TYPE_LAYOUT(FGlobalDistanceFieldParameters, NonVirtual);
public:
	void Bind(const FShaderParameterMap& ParameterMap)
	{
		GlobalDistanceFieldPageAtlasTexture.Bind(ParameterMap, TEXT("GlobalDistanceFieldPageAtlasTexture"));
		GlobalDistanceFieldPageTableTexture.Bind(ParameterMap, TEXT("GlobalDistanceFieldPageTableTexture"));
		GlobalVolumeCenterAndExtent.Bind(ParameterMap,TEXT("GlobalVolumeCenterAndExtent"));
		GlobalVolumeWorldToUVAddAndMul.Bind(ParameterMap,TEXT("GlobalVolumeWorldToUVAddAndMul"));
		GlobalDistanceFieldPageTableScrollOffset.Bind(ParameterMap, TEXT("GlobalDistanceFieldPageTableScrollOffset"));
		GlobalDistanceFieldClipmapSizeInPages.Bind(ParameterMap, TEXT("GlobalDistanceFieldClipmapSizeInPages"));
		GlobalDistanceFieldInvPageAtlasSize.Bind(ParameterMap, TEXT("GlobalDistanceFieldInvPageAtlasSize"));
		GlobalVolumeDimension.Bind(ParameterMap,TEXT("GlobalVolumeDimension"));
		GlobalVolumeTexelSize.Bind(ParameterMap,TEXT("GlobalVolumeTexelSize"));
		MaxGlobalDFAOConeDistance.Bind(ParameterMap,TEXT("MaxGlobalDFAOConeDistance"));
		NumGlobalSDFClipmaps.Bind(ParameterMap,TEXT("NumGlobalSDFClipmaps"));
	}

	bool IsBound() const
	{
		return GlobalVolumeCenterAndExtent.IsBound() || GlobalVolumeWorldToUVAddAndMul.IsBound();
	}

	friend FArchive& operator<<(FArchive& Ar,FGlobalDistanceFieldParameters& Parameters)
	{
		Ar << Parameters.GlobalDistanceFieldPageAtlasTexture;
		Ar << Parameters.GlobalDistanceFieldPageTableTexture;
		Ar << Parameters.GlobalVolumeCenterAndExtent;
		Ar << Parameters.GlobalVolumeWorldToUVAddAndMul;
		Ar << Parameters.GlobalDistanceFieldPageTableScrollOffset;
		Ar << Parameters.GlobalDistanceFieldClipmapSizeInPages;
		Ar << Parameters.GlobalDistanceFieldInvPageAtlasSize;
		Ar << Parameters.GlobalVolumeDimension;
		Ar << Parameters.GlobalVolumeTexelSize;
		Ar << Parameters.MaxGlobalDFAOConeDistance;
		Ar << Parameters.NumGlobalSDFClipmaps;
		return Ar;
	}

	template<typename ShaderRHIParamRef>
	FORCEINLINE_DEBUGGABLE void Set(FRHICommandList& RHICmdList, const ShaderRHIParamRef ShaderRHI, const FGlobalDistanceFieldParameterData& ParameterData) const
	{
		if (IsBound())
		{
			SetTextureParameter(RHICmdList, ShaderRHI, GlobalDistanceFieldPageAtlasTexture, ParameterData.PageAtlasTexture ? ParameterData.PageAtlasTexture : GBlackVolumeTexture->TextureRHI.GetReference());
			SetTextureParameter(RHICmdList, ShaderRHI, GlobalDistanceFieldPageTableTexture, ParameterData.PageTableTexture ? ParameterData.PageTableTexture : GBlackVolumeTexture->TextureRHI.GetReference());

			SetShaderValueArray(RHICmdList, ShaderRHI, GlobalVolumeCenterAndExtent, ParameterData.CenterAndExtent, GMaxGlobalDistanceFieldClipmaps);
			SetShaderValueArray(RHICmdList, ShaderRHI, GlobalVolumeWorldToUVAddAndMul, ParameterData.WorldToUVAddAndMul, GMaxGlobalDistanceFieldClipmaps);
			SetShaderValueArray(RHICmdList, ShaderRHI, GlobalDistanceFieldPageTableScrollOffset, ParameterData.PageTableScrollOffset, GMaxGlobalDistanceFieldClipmaps);
			SetShaderValue(RHICmdList, ShaderRHI, GlobalDistanceFieldClipmapSizeInPages, ParameterData.ClipmapSizeInPages);
			SetShaderValue(RHICmdList, ShaderRHI, GlobalDistanceFieldInvPageAtlasSize, ParameterData.InvPageAtlasSize);
			SetShaderValue(RHICmdList, ShaderRHI, GlobalVolumeDimension, ParameterData.GlobalDFResolution);
			SetShaderValue(RHICmdList, ShaderRHI, GlobalVolumeTexelSize, 1.0f / ParameterData.GlobalDFResolution);
			SetShaderValue(RHICmdList, ShaderRHI, MaxGlobalDFAOConeDistance, ParameterData.MaxDFAOConeDistance);
			SetShaderValue(RHICmdList, ShaderRHI, NumGlobalSDFClipmaps, ParameterData.NumGlobalSDFClipmaps);
		}
	}

private:

	LAYOUT_FIELD(FShaderResourceParameter, GlobalDistanceFieldPageAtlasTexture)
	LAYOUT_FIELD(FShaderResourceParameter, GlobalDistanceFieldPageTableTexture)
	LAYOUT_FIELD(FShaderParameter, GlobalVolumeCenterAndExtent)
	LAYOUT_FIELD(FShaderParameter, GlobalVolumeWorldToUVAddAndMul)
	LAYOUT_FIELD(FShaderParameter, GlobalDistanceFieldPageTableScrollOffset)
	LAYOUT_FIELD(FShaderParameter, GlobalDistanceFieldClipmapSizeInPages)	
	LAYOUT_FIELD(FShaderParameter, GlobalDistanceFieldInvPageAtlasSize)
	LAYOUT_FIELD(FShaderParameter, GlobalVolumeDimension)
	LAYOUT_FIELD(FShaderParameter, GlobalVolumeTexelSize)
	LAYOUT_FIELD(FShaderParameter, MaxGlobalDFAOConeDistance)
	LAYOUT_FIELD(FShaderParameter, NumGlobalSDFClipmaps)
};
