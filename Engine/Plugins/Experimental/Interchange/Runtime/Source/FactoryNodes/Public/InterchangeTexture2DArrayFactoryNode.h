// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InterchangeTextureFactoryNode.h"
#include "Nodes/InterchangeBaseNode.h"

#if WITH_ENGINE
#include "Engine/Texture.h"
#include "Engine/Texture2DArray.h"
#endif


#include "InterchangeTexture2DArrayFactoryNode.generated.h"

UCLASS(BlueprintType, Experimental)
class INTERCHANGEFACTORYNODES_API UInterchangeTexture2DArrayFactoryNode : public UInterchangeTextureFactoryNode
{
	GENERATED_BODY()

public:

	virtual bool SetCustomAddressX(const uint8 AttributeValue, bool bAddApplyDelegate = true) override
	{
#if WITH_EDITORONLY_DATA
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTexture2DArrayFactoryNode, AddressX, uint8, UTexture2DArray)
#else
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(AddressX, uint8)
#endif
	}

	virtual bool SetCustomAddressY(const uint8 AttributeValue, bool bAddApplyDelegate = true) override
	{
#if WITH_EDITORONLY_DATA
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTexture2DArrayFactoryNode, AddressY, uint8, UTexture2DArray)
#else
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(AddressY, uint8)
#endif
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustomAddressZ(uint8& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(AddressZ, uint8);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustomAddressZ(const uint8 AttributeValue, bool bAddApplyDelegate = true)
	{
#if WITH_EDITORONLY_DATA
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTexture2DArrayFactoryNode, AddressZ, uint8, UTexture2DArray)
#else
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(AddressZ, uint8)
#endif
	}


private:
#if WITH_ENGINE
	virtual void FillAssetClassFromClassName(const FString& ClassName) override
	{
		if (ClassName.Equals(UTexture2DArray::StaticClass()->GetName()))
		{
			AssetClass = UTexture2DArray::StaticClass();
			bIsTextureNodeClassInitialized = true;
		}
	}
#endif

	// Addressing
	IMPLEMENT_NODE_ATTRIBUTE_KEY(AddressX);
	IMPLEMENT_NODE_ATTRIBUTE_KEY(AddressY);
	IMPLEMENT_NODE_ATTRIBUTE_KEY(AddressZ);

};