// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusSceneComponentSource.h"

#include "Components/SceneComponent.h"


#define LOCTEXT_NAMESPACE "OptimusSceneComponentSource"


FText UOptimusSceneComponentSource::GetDisplayName() const
{
	return LOCTEXT("SceneComponent", "Scene Component");
}


TSubclassOf<UActorComponent> UOptimusSceneComponentSource::GetComponentClass() const
{
	return USceneComponent::StaticClass();
}


TArray<FName> UOptimusSceneComponentSource::GetExecutionContexts() const
{
	return {};
}


#undef LOCTEXT_NAMESPACE
