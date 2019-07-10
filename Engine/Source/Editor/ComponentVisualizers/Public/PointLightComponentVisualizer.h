// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComponentVisualizer.h"

class FPrimitiveDrawInterface;
class FSceneView;
class UTextureLightProfile;

class FTextureLightProfileVisualizer
{
public:
	FTextureLightProfileVisualizer();

	void DrawVisualization(UTextureLightProfile* TextureLightProfile, const FTransform& LightTM, const FSceneView* View, FPrimitiveDrawInterface* PDI);

private:
	void UpdateIntensitiesCache(UTextureLightProfile* TextureLightProfile, const FTransform& LightTM);

	const UTextureLightProfile* CachedLightProfile;
	TArray< float > IntensitiesCache;
};

class COMPONENTVISUALIZERS_API FPointLightComponentVisualizer : public FComponentVisualizer
{
public:
	//~ Begin FComponentVisualizer Interface
	virtual void DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	//~ End FComponentVisualizer Interface

private:
	FTextureLightProfileVisualizer LightProfileVisualizer;
};
