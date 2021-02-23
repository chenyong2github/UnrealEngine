// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "UObject/Interface.h"

#include "AssetBackedTarget.generated.h"

UINTERFACE()
class INTERACTIVETOOLSFRAMEWORK_API UAssetBackedTarget : public UInterface
{
	GENERATED_BODY()
};

class INTERACTIVETOOLSFRAMEWORK_API IAssetBackedTarget
{
	GENERATED_BODY()

public:
	/**
	 * @return the underlying source asset for this Target.
	 */
	virtual UObject* GetSourceData() const = 0;
};