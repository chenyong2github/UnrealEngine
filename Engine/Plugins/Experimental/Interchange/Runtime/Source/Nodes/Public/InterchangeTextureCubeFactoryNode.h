// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InterchangeTextureFactoryNode.h"

#if WITH_ENGINE
#include "Engine/TextureCube.h"
#endif


#include "InterchangeTextureCubeFactoryNode.generated.h"

UCLASS(BlueprintType, Experimental)
class INTERCHANGENODES_API UInterchangeTextureCubeFactoryNode : public UInterchangeTextureFactoryNode
{
	GENERATED_BODY()

private:

#if WITH_ENGINE
	virtual void FillAssetClassFromClassName(const FString& ClassName) override
	{
		if (ClassName.Equals(UTextureCube::StaticClass()->GetName()))
		{
			AssetClass = UTextureCube::StaticClass();
			bIsTextureNodeClassInitialized = true;
		}
	}
#endif

};