// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HeightfieldLighting.h
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RenderResource.h"
#include "ShaderParameters.h"
#include "Engine/Texture2D.h"
#include "RHIStaticStates.h"
#include "RendererInterface.h"
#include "PrimitiveSceneProxy.h"
#include "RenderGraphResources.h"

class FAOScreenGridResources;
class FDistanceFieldAOParameters;
class FLightSceneInfo;
class FProjectedShadowInfo;
class FScene;
class FViewInfo;
class FGlobalDistanceFieldClipmap;
struct Rect;

class FHeightfieldLightingAtlas : public FRenderResource
{
public:

	TRefCountPtr<IPooledRenderTarget> Height;
	TRefCountPtr<IPooledRenderTarget> Normal;
	TRefCountPtr<IPooledRenderTarget> DiffuseColor;
	TRefCountPtr<IPooledRenderTarget> DirectionalLightShadowing;
	TRefCountPtr<IPooledRenderTarget> Lighting;

	FHeightfieldLightingAtlas() :
		AtlasSize(FIntPoint(0, 0))
	{}

	virtual void InitDynamicRHI();
	virtual void ReleaseDynamicRHI();

	void InitializeForSize(FIntPoint InAtlasSize);

	FIntPoint GetAtlasSize() const { return AtlasSize; }

private:

	FIntPoint AtlasSize;
};

class FHeightfieldComponentTextures
{
public:

	FHeightfieldComponentTextures(UTexture2D* InHeightAndNormal, UTexture2D* InDiffuseColor, UTexture2D* InVisibility) :
		HeightAndNormal(InHeightAndNormal),
		DiffuseColor(InDiffuseColor),
		Visibility(InVisibility)
	{}

	FORCEINLINE bool operator==(FHeightfieldComponentTextures Other) const
	{
		return HeightAndNormal == Other.HeightAndNormal && DiffuseColor == Other.DiffuseColor && Visibility == Other.Visibility;
	}

	FORCEINLINE friend uint32 GetTypeHash(FHeightfieldComponentTextures ComponentTextures)
	{
		return GetTypeHash(ComponentTextures.HeightAndNormal);
	}

	UTexture2D* HeightAndNormal;
	UTexture2D* DiffuseColor;
	UTexture2D* Visibility;
};

class FHeightfieldDescription
{
public:
	FIntRect Rect;
	int32 DownsampleFactor;
	FIntRect DownsampledRect;

	TMap<FHeightfieldComponentTextures, TArray<FHeightfieldComponentDescription>> ComponentDescriptions;

	FHeightfieldDescription() :
		Rect(FIntRect(0, 0, 0, 0)),
		DownsampleFactor(1),
		DownsampledRect(FIntRect(0, 0, 0, 0))
	{}
};

class FHeightfieldLightingViewInfo
{
public:

	FHeightfieldLightingViewInfo()
	{}

	void SetupVisibleHeightfields(const FViewInfo& View, FRDGBuilder& GraphBuilder);

	void SetupHeightfieldsForScene(const FScene& Scene);

	void ClearShadowing(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FLightSceneInfo& LightSceneInfo) const;

	void ComputeShadowMapShadowing(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FProjectedShadowInfo* ProjectedShadowInfo) const;

	void ComputeLighting(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FLightSceneInfo& LightSceneInfo) const;

	void ComputeOcclusionForScreenGrid(
		const FViewInfo& View, 
		FRHICommandListImmediate& RHICmdList, 
		FRHITexture* DistanceFieldNormal,
		const class FAOScreenGridResources& ScreenGridResources,
		const class FDistanceFieldAOParameters& Parameters) const;

	void ComputeIrradianceForScreenGrid(
		const FViewInfo& View, 
		FRHICommandListImmediate& RHICmdList,
		FRHITexture* DistanceFieldNormal,
		const FAOScreenGridResources& ScreenGridResources,
		const FDistanceFieldAOParameters& Parameters) const;

private:

	FHeightfieldDescription Heightfield;
};

extern FRHIShaderResourceView* GetHeightfieldDescriptionsSRV();

class FHeightfieldDescriptionParameters
{
	DECLARE_TYPE_LAYOUT(FHeightfieldDescriptionParameters, NonVirtual);
public:
	void Bind(const FShaderParameterMap& ParameterMap)
	{
		HeightfieldDescriptions.Bind(ParameterMap, TEXT("HeightfieldDescriptions"));
		NumHeightfields.Bind(ParameterMap, TEXT("NumHeightfields"));
	}

	friend FArchive& operator<<(FArchive& Ar, FHeightfieldDescriptionParameters& Parameters)
	{
		Ar << Parameters.HeightfieldDescriptions;
		Ar << Parameters.NumHeightfields;
		return Ar;
	}

	template<typename ShaderRHIParamRef>
	void Set(FRHICommandList& RHICmdList, const ShaderRHIParamRef ShaderRHI, FRHIShaderResourceView* HeightfieldDescriptionsValue, int32 NumHeightfieldsValue)
	{
		SetSRVParameter(RHICmdList, ShaderRHI, HeightfieldDescriptions, HeightfieldDescriptionsValue);
		SetShaderValue(RHICmdList, ShaderRHI, NumHeightfields, NumHeightfieldsValue);
	}

private:
	
		LAYOUT_FIELD(FShaderResourceParameter, HeightfieldDescriptions)
		LAYOUT_FIELD(FShaderParameter, NumHeightfields)
	
};

class FHeightfieldTextureParameters
{
	DECLARE_TYPE_LAYOUT(FHeightfieldTextureParameters, NonVirtual);
public:
	void Bind(const FShaderParameterMap& ParameterMap)
	{
		HeightfieldTexture.Bind(ParameterMap, TEXT("HeightfieldTexture"));
		HeightfieldSampler.Bind(ParameterMap, TEXT("HeightfieldSampler"));
		DiffuseColorTexture.Bind(ParameterMap, TEXT("DiffuseColorTexture"));
		DiffuseColorSampler.Bind(ParameterMap, TEXT("DiffuseColorSampler"));
		VisibilityTexture.Bind(ParameterMap, TEXT("VisibilityTexture"));
		VisibilitySampler.Bind(ParameterMap, TEXT("VisibilitySampler"));
	}

	friend FArchive& operator<<(FArchive& Ar, FHeightfieldTextureParameters& Parameters)
	{
		Ar << Parameters.HeightfieldTexture;
		Ar << Parameters.HeightfieldSampler;
		Ar << Parameters.DiffuseColorTexture;
		Ar << Parameters.DiffuseColorSampler;
		Ar << Parameters.VisibilityTexture;
		Ar << Parameters.VisibilitySampler;
		return Ar;
	}

	template<typename ShaderRHIParamRef>
	void Set(FRHICommandList& RHICmdList, const ShaderRHIParamRef ShaderRHI, UTexture2D* HeightfieldTextureValue, UTexture2D* DiffuseColorTextureValue, UTexture2D* VisibilityTextureValue)
	{
		//@todo - shouldn't filter the heightfield, it's packed
		SetTextureParameter(RHICmdList, ShaderRHI, HeightfieldTexture, HeightfieldSampler, TStaticSamplerState<SF_Bilinear>::GetRHI(), HeightfieldTextureValue->Resource->TextureRHI);

		if (DiffuseColorTextureValue)
		{
			SetTextureParameter(RHICmdList, ShaderRHI, DiffuseColorTexture, DiffuseColorSampler, TStaticSamplerState<SF_Bilinear>::GetRHI(), DiffuseColorTextureValue->Resource->TextureRHI);
		}
		else
		{
			SetTextureParameter(RHICmdList, ShaderRHI, DiffuseColorTexture, DiffuseColorSampler, TStaticSamplerState<SF_Bilinear>::GetRHI(), GBlackTexture->TextureRHI);
		}

		if (VisibilityTextureValue)
		{
			SetTextureParameter(RHICmdList, ShaderRHI, VisibilityTexture, VisibilitySampler, TStaticSamplerState<SF_Bilinear>::GetRHI(), VisibilityTextureValue->Resource->TextureRHI);
		}
		else
		{
			SetTextureParameter(RHICmdList, ShaderRHI, VisibilityTexture, VisibilitySampler, TStaticSamplerState<SF_Bilinear>::GetRHI(), GBlackTexture->TextureRHI);
		}
	}

private:
	
		LAYOUT_FIELD(FShaderResourceParameter, HeightfieldTexture)
		LAYOUT_FIELD(FShaderResourceParameter, HeightfieldSampler)
		LAYOUT_FIELD(FShaderResourceParameter, DiffuseColorTexture)
		LAYOUT_FIELD(FShaderResourceParameter, DiffuseColorSampler)
		LAYOUT_FIELD(FShaderResourceParameter, VisibilityTexture)
		LAYOUT_FIELD(FShaderResourceParameter, VisibilitySampler)
	
};
