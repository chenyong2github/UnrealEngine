// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/BaseNode.h"

#if WITH_ENGINE
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#endif

//Interchange namespace
namespace Interchange
{

struct FTextureNodeStaticData : public FBaseNodeStaticData
{
	static const FAttributeKey& PayloadSourceFileKey()
	{
		static FAttributeKey AttributeKey(TEXT("__PayloadSourceFile__"));
		return AttributeKey;
	}
};

class FTextureNode : public FBaseNode
{
public:
	/**
	 * Construct FTextureNode
	 * @param: UniqueID - The uniqueId for this node
	 * @param DisplayLabel - The name of the node
	 * @param InAssetClass - The class the texture factory will create for this node.
	 */
	FTextureNode(const FNodeUniqueID& UniqueID, const FName& DisplayLabel, const TSubclassOf<UTexture>& InAssetClass)
	: FBaseNode(UniqueID, DisplayLabel)
	, AssetClass(InAssetClass)
	{
	}

	/**
	 * Return the node type name of the class, we use this when reporting error
	 */
	virtual FString GetTypeName() const override
	{
		const FString TypeName = TEXT("TextureNode");
		return TypeName;
	}

	/** Get the class this node want to create */
	virtual class UClass* GetAssetClass() const override
	{
		return AssetClass.Get() != nullptr ? AssetClass.Get() : UTexture::StaticClass();
	}

	virtual FGuid GetHash() const override
	{
		return Attributes.GetStorageHash();
	}

	/** Texture node Interface Begin */

	virtual const TOptional<FString> GetPayLoadKey() const
	{
		if (!Attributes.ContainAttribute(FTextureNodeStaticData::PayloadSourceFileKey()))
		{
			return TOptional<FString>();
		}
		FAttributeStorage::TAttributeHandle<FString> AttributeHandle = Attributes.GetAttributeHandle<FString>(FTextureNodeStaticData::PayloadSourceFileKey());
		if (!AttributeHandle.IsValid())
		{
			return TOptional<FString>();
		}
		FString PayloadKey;
		EAttributeStorageResult Result = AttributeHandle.Get(PayloadKey);
		if (!IsAttributeStorageResultSuccess(Result))
		{
			LogAttributeStorageErrors(Result, TEXT("FTextureNode.GetPayLoadKey"), FTextureNodeStaticData::PayloadSourceFileKey());
			return TOptional<FString>();
		}
		return TOptional<FString>(PayloadKey);
	}

	virtual void SetPayLoadKey(const FString& PayloadKey)
	{
		EAttributeStorageResult Result = Attributes.RegisterAttribute(FTextureNodeStaticData::PayloadSourceFileKey(), PayloadKey);
		if (!IsAttributeStorageResultSuccess(Result))
		{
			LogAttributeStorageErrors(Result, TEXT("FTextureNode.SetPayLoadKey"), FTextureNodeStaticData::PayloadSourceFileKey());
		}
	}

#if WITH_EDITORONLY_DATA
	//Texture Adjustment
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, AdjustBrightness, float, UTexture,);
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, AdjustBrightnessCurve, float, UTexture,);
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, AdjustVibrance, float, UTexture,);
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, AdjustSaturation, float, UTexture,);
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, AdjustRGBCurve, float, UTexture,);
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, AdjustHue, float, UTexture,);
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, AdjustMinAlpha, float, UTexture,);
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, AdjustMaxAlpha, float, UTexture,);
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, bChromaKeyTexture, bool, UTexture,);
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, ChromaKeyThreshold, float, UTexture,);
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, ChromaKeyColor, FColor, UTexture,);

	//Texture Compression
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, CompressionNoAlpha, bool, UTexture,);
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, DeferCompression, bool, UTexture,);
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, LossyCompressionAmount, uint8, UTexture, TEnumAsByte<ETextureLossyCompressionAmount>);
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, MaxTextureSize, int32, UTexture,);
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, CompressionQuality, uint8, UTexture, TEnumAsByte<enum ETextureCompressionQuality>);
#endif //#if WITH_EDITORONLY_DATA
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, CompressionSettings, uint8, UTexture, TEnumAsByte<enum TextureCompressionSettings>);

#if WITH_EDITORONLY_DATA
	//Texture general
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, bDitherMipMapAlpha, bool, UTexture,);
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, AlphaCoverageThresholds, FVector4, UTexture,);
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, bFlipGreenChannel, bool, UTexture,);
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, bForcePVRTC4, bool, UTexture,);
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, PowerOfTwoMode, uint8, UTexture, TEnumAsByte<enum ETexturePowerOfTwoSetting::Type>);
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, PaddingColor, FColor, UTexture,);
#endif //#if WITH_EDITORONLY_DATA
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, Filter, uint8, UTexture, TEnumAsByte<enum TextureFilter>);
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, MipLoadOptions, uint8, UTexture, ETextureMipLoadOptions);
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, SRGB, bool, UTexture,);
#if WITH_EDITORONLY_DATA
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, bUseLegacyGamma, bool, UTexture,);
#endif //#if WITH_EDITORONLY_DATA
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, VirtualTextureStreaming, bool, UTexture,);
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, AddressX, uint8, UTexture2D, TEnumAsByte<enum TextureAddress>);
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, AddressY, uint8, UTexture2D, TEnumAsByte<enum TextureAddress>);

#if WITH_EDITORONLY_DATA
	//Level of Detail
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, bPreserveBorder, bool, UTexture,);
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, MipGenSettings, uint8, UTexture, TEnumAsByte<enum TextureMipGenSettings>);
#endif //#if WITH_EDITORONLY_DATA
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, LODBias, int32, UTexture,);
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, LODGroup, uint8, UTexture, TEnumAsByte<enum TextureGroup>);
	//TODO support per platform data in the FAttributeStorage, so we can set different value per platform at the pipeline stage, We set only the default value for now
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, Downscale, float, UTexture,);
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, DownscaleOptions, uint8, UTexture, ETextureDownscaleOptions);

#if WITH_EDITORONLY_DATA
	//Compositing
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, CompositeTextureMode, uint8, UTexture, TEnumAsByte<enum ECompositeTextureMode>);
	IMPLEMENT_NODE_ATTRIBUTE(FTextureNode, CompositePower, float, UTexture,);
#endif //#if WITH_EDITORONLY_DATA

protected:
	TSubclassOf<UTexture> AssetClass = nullptr;
};


}