// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InterchangeTextureFactoryNode.h"
#include "Nodes/InterchangeBaseNode.h"

#if WITH_ENGINE
#include "Engine/Texture.h"
#include "Engine/TextureLightProfile.h"
#endif


#include "InterchangeTextureLightProfileFactoryNode.generated.h"

UCLASS(BlueprintType, Experimental)
class INTERCHANGEFACTORYNODES_API UInterchangeTextureLightProfileFactoryNode : public UInterchangeTextureFactoryNode
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustomBrightness(float& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(Brightness, float);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustomBrightness(const float AttributeValue, bool bAddApplyDelegate = true)
	{
#if WITH_EDITORONLY_DATA
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureLightProfileFactoryNode, Brightness, float, UTextureLightProfile)
#else
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(Brightness, float)
#endif
	}


	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool GetCustomTextureMultiplier(float& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(TextureMultiplier, float);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Texture")
	bool SetCustomTextureMultiplier(const float AttributeValue, bool bAddApplyDelegate = true)
	{
#if WITH_EDITORONLY_DATA
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeTextureLightProfileFactoryNode, TextureMultiplier, float, UTextureLightProfile)
#else
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(TextureMultiplier, float)
#endif
	}


private:
#if WITH_ENGINE
	virtual void FillAssetClassFromClassName(const FString& ClassName) override
	{
		if (ClassName.Equals(UTextureLightProfile::StaticClass()->GetName()))
		{
			AssetClass = UTextureLightProfile::StaticClass();
			bIsTextureNodeClassInitialized = true;
		}
	}
#endif

	// Addressing
	IMPLEMENT_NODE_ATTRIBUTE_KEY(Brightness);
	IMPLEMENT_NODE_ATTRIBUTE_KEY(TextureMultiplier);

};