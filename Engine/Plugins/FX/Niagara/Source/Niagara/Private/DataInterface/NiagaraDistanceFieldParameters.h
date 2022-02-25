// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Renderer/Private/DistanceFieldLightingShared.h"

class FDistanceFieldParameters
{
	DECLARE_INLINE_TYPE_LAYOUT(FDistanceFieldParameters, NonVirtual);
public:
	void Bind(const FShaderParameterMap& ParameterMap)
	{
		SceneObjectBounds.Bind(ParameterMap, TEXT("SceneObjectBounds"));
		SceneObjectData.Bind(ParameterMap, TEXT("SceneObjectData"));
		NumSceneObjects.Bind(ParameterMap, TEXT("NumSceneObjects"));
		SceneDistanceFieldAssetData.Bind(ParameterMap, TEXT("SceneDistanceFieldAssetData"));
		DistanceFieldIndirectionTable.Bind(ParameterMap, TEXT("DistanceFieldIndirectionTable"));
		DistanceFieldBrickTexture.Bind(ParameterMap, TEXT("DistanceFieldBrickTexture"));
		DistanceFieldSampler.Bind(ParameterMap, TEXT("DistanceFieldSampler"));
		DistanceFieldBrickSize.Bind(ParameterMap, TEXT("DistanceFieldBrickSize"));
		DistanceFieldUniqueDataBrickSize.Bind(ParameterMap, TEXT("DistanceFieldUniqueDataBrickSize"));
		DistanceFieldBrickAtlasSizeInBricks.Bind(ParameterMap, TEXT("DistanceFieldBrickAtlasSizeInBricks"));
		DistanceFieldBrickAtlasMask.Bind(ParameterMap, TEXT("DistanceFieldBrickAtlasMask"));
		DistanceFieldBrickAtlasSizeLog2.Bind(ParameterMap, TEXT("DistanceFieldBrickAtlasSizeLog2"));
		DistanceFieldBrickAtlasTexelSize.Bind(ParameterMap, TEXT("DistanceFieldBrickAtlasTexelSize"));
		DistanceFieldBrickAtlasHalfTexelSize.Bind(ParameterMap, TEXT("DistanceFieldBrickAtlasHalfTexelSize"));
		DistanceFieldBrickOffsetToAtlasUVScale.Bind(ParameterMap, TEXT("DistanceFieldBrickOffsetToAtlasUVScale"));
		DistanceFieldUniqueDataBrickSizeInAtlasTexels.Bind(ParameterMap, TEXT("DistanceFieldUniqueDataBrickSizeInAtlasTexels"));
	}

	bool IsBound() const
	{
		return SceneDistanceFieldAssetData.IsBound();
	}

	friend FArchive& operator<<(FArchive& Ar, FDistanceFieldParameters& Parameters)
	{
		Ar << Parameters.SceneObjectBounds;
		Ar << Parameters.SceneObjectData;
		Ar << Parameters.NumSceneObjects;
		Ar << Parameters.SceneDistanceFieldAssetData;
		Ar << Parameters.DistanceFieldIndirectionTable;
		Ar << Parameters.DistanceFieldBrickTexture;
		Ar << Parameters.DistanceFieldSampler;
		Ar << Parameters.DistanceFieldBrickSize;
		Ar << Parameters.DistanceFieldUniqueDataBrickSize;
		Ar << Parameters.DistanceFieldBrickAtlasSizeInBricks;
		Ar << Parameters.DistanceFieldBrickAtlasMask;
		Ar << Parameters.DistanceFieldBrickAtlasSizeLog2;
		Ar << Parameters.DistanceFieldBrickAtlasTexelSize;
		Ar << Parameters.DistanceFieldBrickAtlasHalfTexelSize;
		Ar << Parameters.DistanceFieldBrickOffsetToAtlasUVScale;
		Ar << Parameters.DistanceFieldUniqueDataBrickSizeInAtlasTexels;

		return Ar;
	}

	template<typename ShaderRHIParamRef>
	FORCEINLINE_DEBUGGABLE void Set(FRHICommandList& RHICmdList, const ShaderRHIParamRef ShaderRHI, const FDistanceFieldSceneData* ParameterData) const
	{
		if (IsBound() && ParameterData->GetCurrentObjectBuffers() != nullptr)
		{
			SetSRVParameter(RHICmdList, ShaderRHI, SceneObjectBounds, ParameterData->GetCurrentObjectBuffers()->Bounds.SRV);
			SetSRVParameter(RHICmdList, ShaderRHI, SceneObjectData, ParameterData->GetCurrentObjectBuffers()->Data.SRV);
			SetShaderValue(RHICmdList, ShaderRHI, NumSceneObjects, ParameterData->NumObjectsInBuffer);
			SetSRVParameter(RHICmdList, ShaderRHI, SceneDistanceFieldAssetData, ParameterData->AssetDataBuffer.SRV);
			SetSRVParameter(RHICmdList, ShaderRHI, DistanceFieldIndirectionTable, ParameterData->IndirectionTable.SRV);
			SetTextureParameter(RHICmdList, ShaderRHI, DistanceFieldBrickTexture, ParameterData->DistanceFieldBrickVolumeTexture->GetRenderTargetItem().ShaderResourceTexture);
			SetSamplerParameter(RHICmdList, ShaderRHI, DistanceFieldSampler, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
			SetShaderValue(RHICmdList, ShaderRHI, DistanceFieldBrickSize, FVector3f(DistanceField::BrickSize));
			SetShaderValue(RHICmdList, ShaderRHI, DistanceFieldUniqueDataBrickSize, FVector3f(DistanceField::UniqueDataBrickSize));
			SetShaderValue(RHICmdList, ShaderRHI, DistanceFieldBrickAtlasSizeInBricks, ParameterData->BrickTextureDimensionsInBricks);
			SetShaderValue(RHICmdList, ShaderRHI, DistanceFieldBrickAtlasMask, ParameterData->BrickTextureDimensionsInBricks - FIntVector(1));
			SetShaderValue(RHICmdList, ShaderRHI, DistanceFieldBrickAtlasSizeLog2, FIntVector(
				FMath::FloorLog2(ParameterData->BrickTextureDimensionsInBricks.X),
				FMath::FloorLog2(ParameterData->BrickTextureDimensionsInBricks.Y),
				FMath::FloorLog2(ParameterData->BrickTextureDimensionsInBricks.Z)));
			FVector3f DistanceFieldBrickAtlasTexelSizeTmp = FVector3f(1.0f) / FVector3f(ParameterData->BrickTextureDimensionsInBricks * DistanceField::BrickSize);
			SetShaderValue(RHICmdList, ShaderRHI, DistanceFieldBrickAtlasTexelSize, DistanceFieldBrickAtlasTexelSizeTmp);
			SetShaderValue(RHICmdList, ShaderRHI, DistanceFieldBrickAtlasHalfTexelSize, 0.5f * DistanceFieldBrickAtlasTexelSizeTmp);
			SetShaderValue(RHICmdList, ShaderRHI, DistanceFieldBrickOffsetToAtlasUVScale, FVector3f(DistanceField::BrickSize) * DistanceFieldBrickAtlasTexelSizeTmp);
			SetShaderValue(RHICmdList, ShaderRHI, DistanceFieldUniqueDataBrickSizeInAtlasTexels, FVector3f(DistanceField::UniqueDataBrickSize) * DistanceFieldBrickAtlasTexelSizeTmp);
		}
	}

private:

	LAYOUT_FIELD(FShaderResourceParameter, SceneObjectBounds)
		LAYOUT_FIELD(FShaderResourceParameter, SceneObjectData)
		LAYOUT_FIELD(FShaderParameter, NumSceneObjects)
		LAYOUT_FIELD(FShaderResourceParameter, SceneDistanceFieldAssetData)
		LAYOUT_FIELD(FShaderResourceParameter, DistanceFieldIndirectionTable)
		LAYOUT_FIELD(FShaderResourceParameter, DistanceFieldBrickTexture)
		LAYOUT_FIELD(FShaderResourceParameter, DistanceFieldSampler)
		LAYOUT_FIELD(FShaderParameter, DistanceFieldBrickSize)
		LAYOUT_FIELD(FShaderParameter, DistanceFieldUniqueDataBrickSize)
		LAYOUT_FIELD(FShaderParameter, DistanceFieldBrickAtlasSizeInBricks)
		LAYOUT_FIELD(FShaderParameter, DistanceFieldBrickAtlasMask)
		LAYOUT_FIELD(FShaderParameter, DistanceFieldBrickAtlasSizeLog2)
		LAYOUT_FIELD(FShaderParameter, DistanceFieldBrickAtlasTexelSize)
		LAYOUT_FIELD(FShaderParameter, DistanceFieldBrickAtlasHalfTexelSize)
		LAYOUT_FIELD(FShaderParameter, DistanceFieldBrickOffsetToAtlasUVScale)
		LAYOUT_FIELD(FShaderParameter, DistanceFieldUniqueDataBrickSizeInAtlasTexels)
};
