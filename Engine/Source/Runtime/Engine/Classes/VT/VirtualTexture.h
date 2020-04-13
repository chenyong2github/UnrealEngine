// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VirtualTexture.generated.h"

/** Deprecated class */
UCLASS(ClassGroup = Rendering)
class ENGINE_API UVirtualTexture : public UObject
{
	GENERATED_UCLASS_BODY()
	virtual void Serialize(FArchive& Ar) override;
};

/** Deprecated class */
UCLASS(ClassGroup = Rendering)
class ENGINE_API ULightMapVirtualTexture : public UVirtualTexture
{
	GENERATED_UCLASS_BODY()
};
