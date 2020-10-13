// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaterBrushActorInterface.h"
#include "Components/PrimitiveComponent.h"

UWaterBrushActorInterface::UWaterBrushActorInterface(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
}

#if WITH_EDITOR
TArray<UPrimitiveComponent*> IWaterBrushActorInterface::GetBrushRenderableComponents() const
{
	return TArray<UPrimitiveComponent*>();
}
#endif //WITH_EDITOR
