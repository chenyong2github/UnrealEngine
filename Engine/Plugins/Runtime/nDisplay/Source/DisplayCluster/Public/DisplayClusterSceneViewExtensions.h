// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"

/** Contains information about the context in which this scene view extension will be used. */
struct FDisplayClusterSceneViewExtensionContext : public FSceneViewExtensionContext
{
private:

	//~ FSceneViewExtensionContext Interface
	virtual FName GetRTTI() const override { return TEXT("FDisplayClusterSceneViewExtensionContext"); }

public:

	// The id of the nDisplay viewport being rendered.
	FString ViewportId;

	FDisplayClusterSceneViewExtensionContext()
		: FSceneViewExtensionContext(nullptr)
	{}

	FDisplayClusterSceneViewExtensionContext(FViewport* InViewport, const FString& InViewportId)
		: FSceneViewExtensionContext(InViewport)
		, ViewportId(InViewportId)
	{}
};
