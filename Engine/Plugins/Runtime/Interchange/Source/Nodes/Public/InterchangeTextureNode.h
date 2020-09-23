// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/InterchangeBaseNode.h"

#if WITH_ENGINE
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#endif

#include "InterchangeTextureNode.generated.h"

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

}

UCLASS(BlueprintType)
class INTERCHANGENODEPLUGIN_API UInterchangeTextureNode : public UInterchangeBaseNode
{
	GENERATED_BODY()

public:
	UInterchangeTextureNode()
	:UInterchangeBaseNode()
	{
#if WITH_ENGINE
		AssetClass = nullptr;
#endif
	}

	/**
	 * Initialize node data
	 * @param: UniqueID - The uniqueId for this node
	 * @param DisplayLabel - The name of the node
	 * @param InAssetClass - The class the texture factory will create for this node.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	void InitializeTextureNode(const FName& UniqueID, const FName& DisplayLabel, const FString& InAssetClass)
	{
		bIsTextureNodeClassInitialized = false;
		InitializeNode(UniqueID, DisplayLabel);
		FString OperationName = GetTypeName() + TEXT(".SetAssetClassName");
		InterchangePrivateNodeBase::SetCustomAttribute<FString>(Attributes, ClassNameAttributeKey, OperationName, InAssetClass);
		FillAssetClassFromAttribute();
	}

	virtual void Serialize(FArchive& Ar) override
	{
		Super::Serialize(Ar);
#if WITH_ENGINE
		if(Ar.IsLoading())
		{
			//Make sure the class is properly set when we compile with engine, this will set the
			//bIsTextureNodeClassInitialized to true.
			SetTextureNodeClassFromClassAttribute();
		}
#endif //#if WITH_ENGINE
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
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	virtual class UClass* GetAssetClass() const override
	{
		ensure(bIsTextureNodeClassInitialized);
#if WITH_ENGINE
		return AssetClass.Get() != nullptr ? AssetClass.Get() : UTexture::StaticClass();
#else
		return nullptr;
#endif
	}

	virtual FGuid GetHash() const override
	{
		return Attributes.GetStorageHash();
	}

	/** Texture node Interface Begin */
	virtual const TOptional<FString> GetPayLoadKey() const
	{
		if (!Attributes.ContainAttribute(Interchange::FTextureNodeStaticData::PayloadSourceFileKey()))
		{
			return TOptional<FString>();
		}
		Interchange::FAttributeStorage::TAttributeHandle<FString> AttributeHandle = Attributes.GetAttributeHandle<FString>(Interchange::FTextureNodeStaticData::PayloadSourceFileKey());
		if (!AttributeHandle.IsValid())
		{
			return TOptional<FString>();
		}
		FString PayloadKey;
		Interchange::EAttributeStorageResult Result = AttributeHandle.Get(PayloadKey);
		if (!IsAttributeStorageResultSuccess(Result))
		{
			LogAttributeStorageErrors(Result, TEXT("UInterchangeTextureNode.GetPayLoadKey"), Interchange::FTextureNodeStaticData::PayloadSourceFileKey());
			return TOptional<FString>();
		}
		return TOptional<FString>(PayloadKey);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	virtual void SetPayLoadKey(const FString& PayloadKey)
	{
		Interchange::EAttributeStorageResult Result = Attributes.RegisterAttribute(Interchange::FTextureNodeStaticData::PayloadSourceFileKey(), PayloadKey);
		if (!IsAttributeStorageResultSuccess(Result))
		{
			LogAttributeStorageErrors(Result, TEXT("UInterchangeTextureNode.SetPayLoadKey"), Interchange::FTextureNodeStaticData::PayloadSourceFileKey());
		}
	}


public:
	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustomAdjustBrightness(float& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(AdjustBrightness, float);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustomAdjustBrightness(const float& AttributeValue, bool bAddApplyDelegate = true)
	{
#if WITH_EDITORONLY_DATA
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, AdjustBrightness, float, UTexture)
#else
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(AdjustBrightness, float)
#endif
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustomAdjustBrightnessCurve(float& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(AdjustBrightnessCurve, float);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustomAdjustBrightnessCurve(const float& AttributeValue, bool bAddApplyDelegate = true)
	{
#if WITH_EDITORONLY_DATA
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, AdjustBrightnessCurve, float, UTexture)
#else
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(AdjustBrightnessCurve, float)
#endif
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustomAdjustVibrance(float& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(AdjustVibrance, float);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustomAdjustVibrance(const float& AttributeValue, bool bAddApplyDelegate = true)
	{
#if WITH_EDITORONLY_DATA
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, AdjustVibrance, float, UTexture)
#else
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(AdjustVibrance, float)
#endif
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustomAdjustSaturation(float& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(AdjustSaturation, float);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustomAdjustSaturation(const float& AttributeValue, bool bAddApplyDelegate = true)
	{
#if WITH_EDITORONLY_DATA
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, AdjustSaturation, float, UTexture)
#else
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(AdjustSaturation, float)
#endif
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustomAdjustRGBCurve(float& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(AdjustRGBCurve, float);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustomAdjustRGBCurve(const float& AttributeValue, bool bAddApplyDelegate = true)
	{
#if WITH_EDITORONLY_DATA
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, AdjustRGBCurve, float, UTexture)
#else
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(AdjustRGBCurve, float)
#endif
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustomAdjustHue(float& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(AdjustHue, float);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustomAdjustHue(const float& AttributeValue, bool bAddApplyDelegate = true)
	{
#if WITH_EDITORONLY_DATA
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, AdjustHue, float, UTexture)
#else
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(AdjustHue, float)
#endif
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustomAdjustMinAlpha(float& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(AdjustMinAlpha, float);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustomAdjustMinAlpha(const float& AttributeValue, bool bAddApplyDelegate = true)
	{
#if WITH_EDITORONLY_DATA
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, AdjustMinAlpha, float, UTexture)
#else
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(AdjustMinAlpha, float)
#endif
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustomAdjustMaxAlpha(float& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(AdjustMaxAlpha, float);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustomAdjustMaxAlpha(const float& AttributeValue, bool bAddApplyDelegate = true)
	{
#if WITH_EDITORONLY_DATA
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, AdjustMaxAlpha, float, UTexture)
#else
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(AdjustMaxAlpha, float)
#endif
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustombChromaKeyTexture(bool& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(bChromaKeyTexture, bool);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustombChromaKeyTexture(const bool& AttributeValue, bool bAddApplyDelegate = true)
	{
#if WITH_EDITORONLY_DATA
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, bChromaKeyTexture, bool, UTexture)
#else
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(bChromaKeyTexture, bool)
#endif
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustomChromaKeyThreshold(float& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(ChromaKeyThreshold, float);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustomChromaKeyThreshold(const float& AttributeValue, bool bAddApplyDelegate = true)
	{
#if WITH_EDITORONLY_DATA
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, ChromaKeyThreshold, float, UTexture)
#else
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(ChromaKeyThreshold, float)
#endif
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustomChromaKeyColor(FColor& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(ChromaKeyColor, FColor);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustomChromaKeyColor(const FColor& AttributeValue, bool bAddApplyDelegate = true)
	{
#if WITH_EDITORONLY_DATA
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, ChromaKeyColor, FColor, UTexture)
#else
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(ChromaKeyColor, FColor)
#endif
	}

	//////////////////////////////////////////////////////////////////////////
	//Texture Compression Begin

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustomCompressionNoAlpha(bool& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(CompressionNoAlpha, bool);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustomCompressionNoAlpha(const bool& AttributeValue, bool bAddApplyDelegate = true)
	{
#if WITH_EDITORONLY_DATA
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, CompressionNoAlpha, bool, UTexture)
#else
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(CompressionNoAlpha, bool)
#endif
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustomDeferCompression(bool& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(DeferCompression, bool);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustomDeferCompression(const bool& AttributeValue, bool bAddApplyDelegate = true)
	{
#if WITH_EDITORONLY_DATA
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, DeferCompression, bool, UTexture)
#else
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(DeferCompression, bool)
#endif
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustomLossyCompressionAmount(uint8& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(LossyCompressionAmount, uint8);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustomLossyCompressionAmount(const uint8& AttributeValue, bool bAddApplyDelegate = true)
	{
#if WITH_EDITORONLY_DATA
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, LossyCompressionAmount, uint8, UTexture)
#else
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(LossyCompressionAmount, uint8)
#endif
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustomMaxTextureSize(int32& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(MaxTextureSize, int32);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustomMaxTextureSize(const int32& AttributeValue, bool bAddApplyDelegate = true)
	{
#if WITH_EDITORONLY_DATA
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, MaxTextureSize, int32, UTexture)
#else
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(MaxTextureSize, int32)
#endif
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustomCompressionQuality(uint8& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(CompressionQuality, uint8);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustomCompressionQuality(const uint8& AttributeValue, bool bAddApplyDelegate = true)
	{
#if WITH_EDITORONLY_DATA
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, CompressionQuality, uint8, UTexture)
#else
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(CompressionQuality, uint8)
#endif
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustomCompressionSettings(uint8& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(CompressionSettings, uint8);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustomCompressionSettings(const uint8& AttributeValue, bool bAddApplyDelegate = true)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, CompressionSettings, uint8, UTexture)
	}

	//////////////////////////////////////////////////////////////////////////
	//Texture general

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustombDitherMipMapAlpha(bool& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(bDitherMipMapAlpha, bool);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustombDitherMipMapAlpha(const bool& AttributeValue, bool bAddApplyDelegate = true)
	{
#if WITH_EDITORONLY_DATA
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, bDitherMipMapAlpha, bool, UTexture)
#else
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(bDitherMipMapAlpha, bool)
#endif
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustomAlphaCoverageThresholds(FVector4& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(AlphaCoverageThresholds, FVector4);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustomAlphaCoverageThresholds(const FVector4& AttributeValue, bool bAddApplyDelegate = true)
	{
#if WITH_EDITORONLY_DATA
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, AlphaCoverageThresholds, FVector4, UTexture)
#else
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(AlphaCoverageThresholds, FVector4)
#endif
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustombFlipGreenChannel(bool& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(bFlipGreenChannel, bool);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustombFlipGreenChannel(const bool& AttributeValue, bool bAddApplyDelegate = true)
	{
#if WITH_EDITORONLY_DATA
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, bFlipGreenChannel, bool, UTexture)
#else
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(bFlipGreenChannel, bool)
#endif
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustombForcePVRTC4(bool& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(bForcePVRTC4, bool);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustombForcePVRTC4(const bool& AttributeValue, bool bAddApplyDelegate = true)
	{
#if WITH_EDITORONLY_DATA
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, bForcePVRTC4, bool, UTexture)
#else
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(bForcePVRTC4, bool)
#endif
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustomPowerOfTwoMode(uint8& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(PowerOfTwoMode, uint8);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustomPowerOfTwoMode(const uint8& AttributeValue, bool bAddApplyDelegate = true)
	{
#if WITH_EDITORONLY_DATA
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, PowerOfTwoMode, uint8, UTexture)
#else
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(PowerOfTwoMode, uint8)
#endif
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustomPaddingColor(FColor& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(PaddingColor, FColor);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustomPaddingColor(const FColor& AttributeValue, bool bAddApplyDelegate = true)
	{
#if WITH_EDITORONLY_DATA
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, PaddingColor, FColor, UTexture)
#else
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(PaddingColor, FColor)
#endif
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustomFilter(uint8& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(Filter, uint8);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustomFilter(const uint8& AttributeValue, bool bAddApplyDelegate = true)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, Filter, uint8, UTexture)
	}
	
	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustomMipLoadOptions(uint8& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(MipLoadOptions, uint8);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustomMipLoadOptions(const uint8& AttributeValue, bool bAddApplyDelegate = true)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, MipLoadOptions, uint8, UTexture)
	}
	
	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustomSRGB(bool& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(SRGB, bool);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustomSRGB(const bool& AttributeValue, bool bAddApplyDelegate = true)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, SRGB, bool, UTexture)
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustombUseLegacyGamma(bool& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(bUseLegacyGamma, bool);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustombUseLegacyGamma(const bool& AttributeValue, bool bAddApplyDelegate = true)
	{
#if WITH_EDITORONLY_DATA
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, bUseLegacyGamma, bool, UTexture)
#else
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(bUseLegacyGamma, bool)
#endif
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustomVirtualTextureStreaming(bool& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(VirtualTextureStreaming, bool);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustomVirtualTextureStreaming(const bool& AttributeValue, bool bAddApplyDelegate = true)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, VirtualTextureStreaming, bool, UTexture)
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustomAddressX(uint8& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(AddressX, uint8);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustomAddressX(const uint8& AttributeValue, bool bAddApplyDelegate = true)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, AddressX, uint8, UTexture2D)
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustomAddressY(uint8& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(AddressY, uint8);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustomAddressY(const uint8& AttributeValue, bool bAddApplyDelegate = true)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, AddressY, uint8, UTexture2D)
	}

	//////////////////////////////////////////////////////////////////////////
	//Level of Detail

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustombPreserveBorder(bool& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(bPreserveBorder, bool);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustombPreserveBorder(const bool& AttributeValue, bool bAddApplyDelegate = true)
	{
#if WITH_EDITORONLY_DATA
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, bPreserveBorder, bool, UTexture)
#else
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(bPreserveBorder, bool)
#endif
	}
	
	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustomMipGenSettings(uint8& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(MipGenSettings, uint8);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustomMipGenSettings(const uint8& AttributeValue, bool bAddApplyDelegate = true)
	{
#if WITH_EDITORONLY_DATA
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, MipGenSettings, uint8, UTexture)
#else
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(MipGenSettings, uint8)
#endif
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustomLODBias(int32& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(LODBias, int32);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustomLODBias(const int32& AttributeValue, bool bAddApplyDelegate = true)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, LODBias, int32, UTexture)
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustomLODGroup(uint8& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(LODGroup, uint8);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustomLODGroup(const uint8& AttributeValue, bool bAddApplyDelegate = true)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, LODGroup, uint8, UTexture)
	}

	//TODO support per platform data in the FAttributeStorage, so we can set different value per platform at the pipeline stage, We set only the default value for now
	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustomDownscale(float& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(Downscale, float);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustomDownscale(const float& AttributeValue, bool bAddApplyDelegate = true)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, Downscale, float, UTexture)
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
		bool GetCustomDownscaleOptions(uint8& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(DownscaleOptions, uint8);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
		bool SetCustomDownscaleOptions(const uint8& AttributeValue, bool bAddApplyDelegate = true)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, DownscaleOptions, uint8, UTexture)
	}

	//////////////////////////////////////////////////////////////////////////
	//Compositing
	
	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustomCompositeTextureMode(uint8& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(CompositeTextureMode, uint8);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustomCompositeTextureMode(const uint8& AttributeValue, bool bAddApplyDelegate = true)
	{
#if WITH_EDITORONLY_DATA
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, CompositeTextureMode, uint8, UTexture)
#else
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(CompositeTextureMode, uint8)
#endif
	}
	
	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustomCompositePower(float& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(CompositePower, float);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustomCompositePower(const float& AttributeValue, bool bAddApplyDelegate = true)
	{
#if WITH_EDITORONLY_DATA
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureNode, CompositePower, float, UTexture)
#else
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(CompositePower, float)
#endif
	}

private:

	void FillAssetClassFromAttribute()
	{
#if WITH_ENGINE
		FString OperationName = GetTypeName() + TEXT(".GetAssetClassName");
		FString ClassName;
		InterchangePrivateNodeBase::GetCustomAttribute<FString>(Attributes, ClassNameAttributeKey, OperationName, ClassName);
		if (ClassName.Equals(UTexture2D::StaticClass()->GetName()))
		{
			AssetClass = UTexture2D::StaticClass();
			bIsTextureNodeClassInitialized = true;
		}
		else if (ClassName.Equals(UTexture::StaticClass()->GetName()))
		{
			AssetClass = UTexture::StaticClass();
			bIsTextureNodeClassInitialized = true;
		}
#endif
	}

	bool SetTextureNodeClassFromClassAttribute()
	{
		if (!bIsTextureNodeClassInitialized)
		{
			FillAssetClassFromAttribute();
		}
		return bIsTextureNodeClassInitialized;
	}

	bool IsEditorOnlyDataDefined()
	{
#if WITH_EDITORONLY_DATA
		return true;
#else
		return false;
#endif
	}

	const Interchange::FAttributeKey ClassNameAttributeKey = Interchange::FAttributeKey(TEXT("__ClassTypeAttribute__"));

	//Texture Adjustments
	const Interchange::FAttributeKey Macro_CustomAdjustBrightnessKey = Interchange::FAttributeKey(TEXT("AdjustBrightness"));
	const Interchange::FAttributeKey Macro_CustomAdjustBrightnessCurveKey = Interchange::FAttributeKey(TEXT("AdjustBrightnessCurve"));
	const Interchange::FAttributeKey Macro_CustomAdjustVibranceKey = Interchange::FAttributeKey(TEXT("AdjustVibrance"));
	const Interchange::FAttributeKey Macro_CustomAdjustSaturationKey = Interchange::FAttributeKey(TEXT("AdjustSaturation"));
	const Interchange::FAttributeKey Macro_CustomAdjustRGBCurveKey = Interchange::FAttributeKey(TEXT("AdjustRGBCurve"));
	const Interchange::FAttributeKey Macro_CustomAdjustHueKey = Interchange::FAttributeKey(TEXT("AdjustHue"));
	const Interchange::FAttributeKey Macro_CustomAdjustMinAlphaKey = Interchange::FAttributeKey(TEXT("AdjustMinAlpha"));
	const Interchange::FAttributeKey Macro_CustomAdjustMaxAlphaKey = Interchange::FAttributeKey(TEXT("AdjustMaxAlpha"));
	const Interchange::FAttributeKey Macro_CustombChromaKeyTextureKey = Interchange::FAttributeKey(TEXT("bChromaKeyTexture"));
	const Interchange::FAttributeKey Macro_CustomChromaKeyThresholdKey = Interchange::FAttributeKey(TEXT("ChromaKeyThreshold"));
	const Interchange::FAttributeKey Macro_CustomChromaKeyColorKey = Interchange::FAttributeKey(TEXT("ChromaKeyColor"));

	//Texture Compression
	const Interchange::FAttributeKey Macro_CustomCompressionNoAlphaKey = Interchange::FAttributeKey(TEXT("CompressionNoAlpha"));
	const Interchange::FAttributeKey Macro_CustomDeferCompressionKey = Interchange::FAttributeKey(TEXT("DeferCompression"));
	const Interchange::FAttributeKey Macro_CustomLossyCompressionAmountKey = Interchange::FAttributeKey(TEXT("LossyCompressionAmount"));
	const Interchange::FAttributeKey Macro_CustomMaxTextureSizeKey = Interchange::FAttributeKey(TEXT("MaxTextureSize"));
	const Interchange::FAttributeKey Macro_CustomCompressionQualityKey = Interchange::FAttributeKey(TEXT("CompressionQuality"));
	const Interchange::FAttributeKey Macro_CustomCompressionSettingsKey = Interchange::FAttributeKey(TEXT("CompressionSettings"));

	//Texture general
	const Interchange::FAttributeKey Macro_CustombDitherMipMapAlphaKey = Interchange::FAttributeKey(TEXT("bDitherMipMapAlpha"));
	const Interchange::FAttributeKey Macro_CustomAlphaCoverageThresholdsKey = Interchange::FAttributeKey(TEXT("AlphaCoverageThresholds"));
	const Interchange::FAttributeKey Macro_CustombFlipGreenChannelKey = Interchange::FAttributeKey(TEXT("bFlipGreenChannel"));
	const Interchange::FAttributeKey Macro_CustombForcePVRTC4Key = Interchange::FAttributeKey(TEXT("bForcePVRTC4"));
	const Interchange::FAttributeKey Macro_CustomPowerOfTwoModeKey = Interchange::FAttributeKey(TEXT("PowerOfTwoMode"));
	const Interchange::FAttributeKey Macro_CustomPaddingColorKey = Interchange::FAttributeKey(TEXT("PaddingColor"));
	const Interchange::FAttributeKey Macro_CustomFilterKey = Interchange::FAttributeKey(TEXT("Filter"));
	const Interchange::FAttributeKey Macro_CustomMipLoadOptionsKey = Interchange::FAttributeKey(TEXT("MipLoadOptions"));
	const Interchange::FAttributeKey Macro_CustomSRGBKey = Interchange::FAttributeKey(TEXT("SRGB"));
	const Interchange::FAttributeKey Macro_CustombUseLegacyGammaKey = Interchange::FAttributeKey(TEXT("bUseLegacyGamma"));
	const Interchange::FAttributeKey Macro_CustomVirtualTextureStreamingKey = Interchange::FAttributeKey(TEXT("VirtualTextureStreaming"));
	const Interchange::FAttributeKey Macro_CustomAddressXKey = Interchange::FAttributeKey(TEXT("AddressX"));
	const Interchange::FAttributeKey Macro_CustomAddressYKey = Interchange::FAttributeKey(TEXT("AddressY"));

	//Level of Detail
	const Interchange::FAttributeKey Macro_CustombPreserveBorderKey = Interchange::FAttributeKey(TEXT("bPreserveBorder"));
	const Interchange::FAttributeKey Macro_CustomMipGenSettingsKey = Interchange::FAttributeKey(TEXT("MipGenSettings"));
	const Interchange::FAttributeKey Macro_CustomLODBiasKey = Interchange::FAttributeKey(TEXT("LODBias"));
	const Interchange::FAttributeKey Macro_CustomLODGroupKey = Interchange::FAttributeKey(TEXT("LODGroup"));
	//TODO support per platform data in the FAttributeStorage, so we can set different value per platform at the pipeline stage, We set only the default value for now
	const Interchange::FAttributeKey Macro_CustomDownscaleKey = Interchange::FAttributeKey(TEXT("Downscale"));
	const Interchange::FAttributeKey Macro_CustomDownscaleOptionsKey = Interchange::FAttributeKey(TEXT("DownscaleOptions"));

	//Compositing
	const Interchange::FAttributeKey Macro_CustomCompositeTextureModeKey = Interchange::FAttributeKey(TEXT("CompositeTextureMode"));
	const Interchange::FAttributeKey Macro_CustomCompositePowerKey = Interchange::FAttributeKey(TEXT("CompositePower"));

#if WITH_EDITORONLY_DATA
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(AdjustBrightness, float, UTexture, );
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(AdjustBrightnessCurve, float, UTexture, );
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(AdjustVibrance, float, UTexture, );
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(AdjustSaturation, float, UTexture, );
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(AdjustRGBCurve, float, UTexture, );
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(AdjustHue, float, UTexture, );
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(AdjustMinAlpha, float, UTexture, );
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(AdjustMaxAlpha, float, UTexture, );
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(bChromaKeyTexture, bool, UTexture, );
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(ChromaKeyThreshold, float, UTexture, );
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(ChromaKeyColor, FColor, UTexture, );

	//Texture Compression
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(CompressionNoAlpha, bool, UTexture, );
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(DeferCompression, bool, UTexture, );
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(LossyCompressionAmount, uint8, UTexture, TEnumAsByte<ETextureLossyCompressionAmount>);
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(MaxTextureSize, int32, UTexture, );
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(CompressionQuality, uint8, UTexture, TEnumAsByte<enum ETextureCompressionQuality>);
#endif
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(CompressionSettings, uint8, UTexture, TEnumAsByte<enum TextureCompressionSettings>);
#if WITH_EDITORONLY_DATA
	//Texture general
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(bDitherMipMapAlpha, bool, UTexture, );
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(AlphaCoverageThresholds, FVector4, UTexture, );
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(bFlipGreenChannel, bool, UTexture, );
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(bForcePVRTC4, bool, UTexture, );
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(PowerOfTwoMode, uint8, UTexture, TEnumAsByte<enum ETexturePowerOfTwoSetting::Type>);
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(PaddingColor, FColor, UTexture, );
#endif //#if WITH_EDITORONLY_DATA
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(Filter, uint8, UTexture, TEnumAsByte<enum TextureFilter>);
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(MipLoadOptions, uint8, UTexture, ETextureMipLoadOptions);
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(SRGB, bool, UTexture, );
#if WITH_EDITORONLY_DATA
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(bUseLegacyGamma, bool, UTexture, );
#endif //#if WITH_EDITORONLY_DATA
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(VirtualTextureStreaming, bool, UTexture, );
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(AddressX, uint8, UTexture2D, TEnumAsByte<enum TextureAddress>);
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(AddressY, uint8, UTexture2D, TEnumAsByte<enum TextureAddress>);
#if WITH_EDITORONLY_DATA
	//Level of Detail
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(bPreserveBorder, bool, UTexture, );
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(MipGenSettings, uint8, UTexture, TEnumAsByte<enum TextureMipGenSettings>);
#endif //#if WITH_EDITORONLY_DATA
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(LODBias, int32, UTexture, );
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(LODGroup, uint8, UTexture, TEnumAsByte<enum TextureGroup>);
	//TODO support per platform data in the FAttributeStorage, so we can set different value per platform at the pipeline stage, We set only the default value for now
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(Downscale, float, UTexture, );
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(DownscaleOptions, uint8, UTexture, ETextureDownscaleOptions);
#if WITH_EDITORONLY_DATA
	//Compositing
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(CompositeTextureMode, uint8, UTexture, TEnumAsByte<enum ECompositeTextureMode>);
	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(CompositePower, float, UTexture, );
#endif //#if WITH_EDITORONLY_DATA
protected:
#if WITH_ENGINE
	TSubclassOf<UTexture> AssetClass = nullptr;
#endif
	bool bIsTextureNodeClassInitialized = false;
};
