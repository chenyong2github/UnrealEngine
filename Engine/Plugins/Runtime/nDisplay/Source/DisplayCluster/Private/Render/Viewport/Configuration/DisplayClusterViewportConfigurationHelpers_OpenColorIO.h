// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FDisplayClusterViewport;
class ADisplayClusterRootActor;

class UDisplayClusterConfigurationViewport;
class UDisplayClusterICVFXCameraComponent;

struct FOpenColorIODisplayConfiguration;
struct FOpenColorIOColorConversionSettings;

class FDisplayClusterViewportConfigurationHelpers_OpenColorIO
{
public:
	static bool UpdateBaseViewport(FDisplayClusterViewport& DstViewport, ADisplayClusterRootActor& RootActor, const UDisplayClusterConfigurationViewport& InViewportConfiguration);
	static bool UpdateICVFXCameraViewport(FDisplayClusterViewport& DstViewport, ADisplayClusterRootActor& RootActor, UDisplayClusterICVFXCameraComponent& InCameraComponent);
	static bool UpdateLightcardViewport(FDisplayClusterViewport& DstViewport, FDisplayClusterViewport& BaseViewport, ADisplayClusterRootActor& RootActor);

private:
	static bool ImplUpdate(FDisplayClusterViewport& DstViewport, const FOpenColorIODisplayConfiguration& InConfiguration);
	static void ImplDisable(FDisplayClusterViewport& DstViewport);

	static bool ImplUpdateOuterViewportOCIO(FDisplayClusterViewport& DstViewport, ADisplayClusterRootActor& RootActor);
};
