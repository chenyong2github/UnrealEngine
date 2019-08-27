// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VirtualTexturing.h"

/** IVirtualTexture implementation that redirects requests to one of two children depending on vLevel. */
class FVirtualTextureLevelRedirector : public IVirtualTexture
{
public:
	FVirtualTextureLevelRedirector(IVirtualTexture* InVirtualTexture0, IVirtualTexture* InVirtualTexture1, int32 InTransitionLevel);
	virtual ~FVirtualTextureLevelRedirector();

	//~ Begin IVirtualTexture Interface.
	virtual FVTRequestPageResult RequestPageData(
		const FVirtualTextureProducerHandle& ProducerHandle,
		uint8 LayerMask,
		uint8 vLevel,
		uint32 vAddress,
		EVTRequestPagePriority Priority
	) override;

	virtual IVirtualTextureFinalizer* ProducePageData(
		FRHICommandListImmediate& RHICmdList,
		ERHIFeatureLevel::Type FeatureLevel,
		EVTProducePageFlags Flags,
		const FVirtualTextureProducerHandle& ProducerHandle,
		uint8 LayerMask,
		uint8 vLevel,
		uint32 vAddress,
		uint64 RequestHandle,
		const FVTProduceTargetLayer* TargetLayers
	) override;
	//~ End IVirtualTexture Interface.

private:
	IVirtualTexture* VirtualTextures[2];
	int32 TransitionLevel;
};
