// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FDisplayClusterViewport;
class ADisplayClusterRootActor;
struct FOpenColorIODisplayConfiguration;

class FDisplayClusterViewportConfigurationHelpers_OpenColorIO
{
public:
	static void Update(FDisplayClusterViewport& DstViewport, ADisplayClusterRootActor& RootActor, const FOpenColorIODisplayConfiguration& InOCIO_Configuration);
};
