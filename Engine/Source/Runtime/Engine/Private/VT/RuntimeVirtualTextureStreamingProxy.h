// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "VT/VirtualTextureBuildSettings.h"
#include "RuntimeVirtualTextureStreamingProxy.generated.h"

/** Streaming virtual texture used to store the low mips in a URuntimeVirtualTexture. */
UCLASS(ClassGroup = Rendering)
class URuntimeVirtualTextureStreamingProxy : public UTexture2D
{
	GENERATED_UCLASS_BODY()

	/** Virtual texture build settings. These should match those of any owning URuntimeVirtualTexture. */
	UPROPERTY()
	FVirtualTextureBuildSettings Settings;

	/** Hash of settings used when building this texture. Used to invalidate when build settings have changed. */
	UPROPERTY()
	uint32 BuildHash;

	//~ Begin UTexture Interface.
	virtual void GetVirtualTextureBuildSettings(FVirtualTextureBuildSettings& OutSettings) const override;
	//~ End UTexture Interface.
};
