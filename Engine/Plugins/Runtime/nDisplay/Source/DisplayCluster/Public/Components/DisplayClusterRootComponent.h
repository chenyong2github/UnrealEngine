// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/SceneComponent.h"
#include "DisplayClusterRootComponent.generated.h"


/**
 * DEPRECATED
 */
UCLASS(ClassGroup = (DisplayCluster), meta = (DeprecatedNode, DeprecationMessage = "This class is now deprecated, please use ADisplayClusterRootActor."))
class DISPLAYCLUSTER_API UDisplayClusterRootComponent : public USceneComponent
{
	GENERATED_BODY()
};
