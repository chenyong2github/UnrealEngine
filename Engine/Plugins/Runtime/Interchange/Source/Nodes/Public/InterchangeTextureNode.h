// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/InterchangeBaseNode.h"

#include "InterchangeTextureNode.generated.h"

//Interchange namespace
namespace UE
{
	namespace Interchange
	{

		struct FTextureNodeStaticData : public FBaseNodeStaticData
		{
			static const FAttributeKey& PayloadSourceFileKey()
			{
				static FAttributeKey AttributeKey(TEXT("__PayloadSourceFile__"));
				return AttributeKey;
			}

			static const FString& GetBaseSourceBlocksKey()
			{
				static FString StringKey(TEXT("SourceBlocks"));
				return StringKey;
			}
		};
	}//ns Interchange
}//ns UE

UCLASS(BlueprintType)
class INTERCHANGENODES_API UInterchangeTextureNode : public UInterchangeBaseNode
{
	GENERATED_BODY()

public:
	UInterchangeTextureNode()
	:UInterchangeBaseNode()
	{
	}

	virtual void PostInitProperties()
	{
		Super::PostInitProperties();
		SourceBlocks.Initialize(Attributes.ToSharedRef(), UE::Interchange::FTextureNodeStaticData::GetBaseSourceBlocksKey());
	}

	/**
	 * Return the node type name of the class, we use this when reporting error
	 */
	virtual FString GetTypeName() const override
	{
		const FString TypeName = TEXT("TextureNode");
		return TypeName;
	}

	virtual FString GetKeyDisplayName(const UE::Interchange::FAttributeKey& NodeAttributeKey) const override
	{
		FString KeyDisplayName = NodeAttributeKey.ToString();
		if (NodeAttributeKey == UE::Interchange::FTextureNodeStaticData::PayloadSourceFileKey())
		{
			return KeyDisplayName = TEXT("Payload Source Key");
		}
		return Super::GetKeyDisplayName(NodeAttributeKey);
	}

	virtual FGuid GetHash() const override
	{
		return Attributes->GetStorageHash();
	}

	/** Texture node Interface Begin */
	virtual const TOptional<FString> GetPayLoadKey() const
	{
		if (!Attributes->ContainAttribute(UE::Interchange::FTextureNodeStaticData::PayloadSourceFileKey()))
		{
			return TOptional<FString>();
		}
		UE::Interchange::FAttributeStorage::TAttributeHandle<FString> AttributeHandle = Attributes->GetAttributeHandle<FString>(UE::Interchange::FTextureNodeStaticData::PayloadSourceFileKey());
		if (!AttributeHandle.IsValid())
		{
			return TOptional<FString>();
		}
		FString PayloadKey;
		UE::Interchange::EAttributeStorageResult Result = AttributeHandle.Get(PayloadKey);
		if (!IsAttributeStorageResultSuccess(Result))
		{
			LogAttributeStorageErrors(Result, TEXT("UInterchangeTextureNode.GetPayLoadKey"), UE::Interchange::FTextureNodeStaticData::PayloadSourceFileKey());
			return TOptional<FString>();
		}
		return TOptional<FString>(PayloadKey);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	virtual void SetPayLoadKey(const FString& PayloadKey)
	{
		UE::Interchange::EAttributeStorageResult Result = Attributes->RegisterAttribute(UE::Interchange::FTextureNodeStaticData::PayloadSourceFileKey(), PayloadKey);
		if (!IsAttributeStorageResultSuccess(Result))
		{
			LogAttributeStorageErrors(Result, TEXT("UInterchangeTextureNode.SetPayLoadKey"), UE::Interchange::FTextureNodeStaticData::PayloadSourceFileKey());
		}
	}

	//////////////////////////////////////////////////////////////////////////
	// UDIMs begin here
	// UDIM base texture use a different model for the source data

	/**
	 * Get the source blocks for the texture
	 * If the map is empty then the texture will be simply be imported as normal texture using the payload key
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture | UDIMs")
	TMap<int32, FString> GetSourceBlocks() const
	{
		return SourceBlocks.ToMap();
	}

	/**
	 * Set the source blocks
	 * Using this will force the texture factory to consider this texture as UDIM.
	 * @param InSourceBlocks The blocks and their source image that compose the whole texture.
	 * The textures must be of the same format and use the same pixel format
	 * The first block inserted in the map be used to determine the accepted texture format and pixel format
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture | UDIMs")
	void SetSourceBlocks(const TMap<int32, FString>& InSourceBlocks)
	{
		SourceBlocks = InSourceBlocks;
	}

	/**
	 * Remove the source block data from texture node
	 * This is a easy way to stop a texture from being imported as a UDIM
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture | UDIMs")
	void ClearSourceBlocksData()
	{
		SourceBlocks.Empty();
	}

	// UDIMs ends here
	//////////////////////////////////////////////////////////////////////////

protected:

	UE::Interchange::TMapAttributeHelper<int32, FString> SourceBlocks;
};
