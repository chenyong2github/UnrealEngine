// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InterchangeTextureFactoryNode.h"

#include "Engine/Texture2D.h"

#include "InterchangeTexture2DFactoryNode.generated.h"

namespace UE::Interchange
{ 
	struct FTexture2DFactoryNodeStaticData : public FBaseNodeStaticData
	{
		static const FString& GetBaseSourceBlocksKey()
		{
			static FString StringKey(TEXT("SourceBlocks"));
			return StringKey;
		}
	};
}//ns UE::Interchange

UCLASS(BlueprintType, Experimental)
class INTERCHANGEFACTORYNODES_API UInterchangeTexture2DFactoryNode : public UInterchangeTextureFactoryNode
{
	GENERATED_BODY()

public:


	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustomAddressX(uint8& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(AddressX, uint8);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustomAddressX(const uint8 AttributeValue, bool bAddApplyDelegate = true)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTexture2DFactoryNode, AddressX, uint8, UTexture2D)
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustomAddressY(uint8& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(AddressY, uint8);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustomAddressY(const uint8 AttributeValue, bool bAddApplyDelegate = true)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTexture2DFactoryNode, AddressY, uint8, UTexture2D)
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
	 * Using this will suggest the pipeline to consider this texture as UDIM and it can chose to pass or not these block to the texture factory node.
	 * @param InSourceBlocks The blocks and their source image that compose the whole texture.
	 * The textures must be of the same format and use the same pixel format
	 * The first block in the map is used to determine the accepted texture format and pixel format
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture | UDIMs")
	void SetSourceBlocks(const TMap<int32, FString>& InSourceBlocks)
	{
		SourceBlocks = InSourceBlocks;
	}

	
	/**
	 * Set the source blocks
	 * Using this will suggest the pipeline to consider this texture as UDIM and it can chose to pass or not these block to the texture factory node.
	 * @param InSourceBlocks The blocks and their source image that compose the whole texture.
	 * The textures must be of the same format and use the same pixel format
	 * The first block in the map is used to determine the accepted texture format and pixel format
	 */
	void SetSourceBlocks(TMap<int32, FString>&& InSourceBlocks)
	{
		SourceBlocks = InSourceBlocks;
	}

	// UDIMs ends here
	//////////////////////////////////////////////////////////////////////////

	virtual UClass* GetObjectClass() const override
	{
		return UTexture2D::StaticClass();
	}

	virtual void PostInitProperties()
	{
		Super::PostInitProperties();
		SourceBlocks.Initialize(Attributes.ToSharedRef(), UE::Interchange::FTexture2DFactoryNodeStaticData::GetBaseSourceBlocksKey());
	}

private:
	IMPLEMENT_NODE_ATTRIBUTE_KEY(AddressX);
	IMPLEMENT_NODE_ATTRIBUTE_KEY(AddressY);

	UE::Interchange::TMapAttributeHelper<int32, FString> SourceBlocks;
};