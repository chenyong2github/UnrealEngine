// Copyright Epic Games, Inc. All Rights Reserved.

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

	/** 
	 * Enables combining layers into a single physical space. 
	 * If this value doesn't match the owning URuntimeVirtualTexture then unwanted physical pools may be allocated.
	 */
	UPROPERTY()
	bool bSinglePhysicalSpace;

	/** Hash of settings used when building this texture. Used to invalidate when build settings have changed. */
	UPROPERTY()
	uint32 BuildHash;

	//~ Begin UTexture Interface.
	virtual void GetVirtualTextureBuildSettings(FVirtualTextureBuildSettings& OutSettings) const override;
	virtual bool IsVirtualTexturedWithSinglePhysicalSpace() const override { return bSinglePhysicalSpace; }
#if WITH_EDITOR
	virtual void BeginCacheForCookedPlatformData(const ITargetPlatform* TargetPlatform) override;
#endif
	//~ End UTexture Interface.
};
