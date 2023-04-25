// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VPFullScreenUserWidget_PostProcessBase.h"
#include "VPFullScreenUserWidget_PostProcessWithSVE.generated.h"

class ISceneViewExtension;
class UMaterialInterface;

/**
 * Renders widget in post process phase by using Scene View Extensions (SVE).
 */
USTRUCT()
struct FVPFullScreenUserWidget_PostProcessWithSVE : public FVPFullScreenUserWidget_PostProcessBase
{
	GENERATED_BODY()

	bool Display(UWorld* World, UUserWidget* Widget, TAttribute<float> InDPIScale);
	void Hide(UWorld* World);
	void Tick(UWorld* World, float DeltaSeconds);

private:

	/** Implements the rendering side */
	TSharedPtr<ISceneViewExtension, ESPMode::ThreadSafe> SceneViewExtension;

	UMaterialInterface* GetPostProcessMaterial() const;
};
