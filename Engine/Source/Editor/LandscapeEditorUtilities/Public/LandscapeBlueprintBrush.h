// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "GameFramework/Actor.h"
#include "LandscapeBlueprintBrushBase.h"

#include "LandscapeBlueprintBrush.generated.h"

UCLASS(Abstract, Blueprintable, hidecategories = (Replication, Input, LOD, Actor, Cooking, Rendering))
class LANDSCAPEEDITORUTILITIES_API ALandscapeBlueprintBrush : public ALandscapeBlueprintBrushBase
{
	GENERATED_UCLASS_BODY()
};