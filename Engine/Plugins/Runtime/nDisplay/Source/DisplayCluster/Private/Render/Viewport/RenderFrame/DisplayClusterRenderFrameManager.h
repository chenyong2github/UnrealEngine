// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FViewport;
class FDisplayClusterViewport;
class FDisplayClusterRenderFrame;
struct FDisplayClusterRenderFrameSettings;

class FDisplayClusterRenderFrameManager
{
public:
	bool BuildRenderFrame(FViewport* InViewport, const FDisplayClusterRenderFrameSettings& InRenderFrameSettings, const TArray<FDisplayClusterViewport*>& InViewports, FDisplayClusterRenderFrame& OutRenderFrame);

private:
	bool FindFrameTargetRect(const TArray<FDisplayClusterViewport*>& InOutViewports, FIntRect& OutFrameTargetRect) const;
	bool BuildSimpleFrame(class FViewport* InViewport, const FDisplayClusterRenderFrameSettings& InRenderFrameSettings, const TArray<FDisplayClusterViewport*>& InViewports, FDisplayClusterRenderFrame& OutRenderFrame);
};


