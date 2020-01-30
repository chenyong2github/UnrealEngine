// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "DefaultStereoLayers.h"

class FWindowsMixedRealityStereoLayers : public FDefaultStereoLayers
{
public:
	FWindowsMixedRealityStereoLayers(const class FAutoRegister& AutoRegister, class FHeadMountedDisplayBase* InHmd) : FDefaultStereoLayers(AutoRegister, InHmd) {}

	//~ IStereoLayers interface
	virtual FLayerDesc GetDebugCanvasLayerDesc(FTextureRHIRef Texture) override;
	virtual bool ShouldCopyDebugLayersToSpectatorScreen() const override { return true; }
};