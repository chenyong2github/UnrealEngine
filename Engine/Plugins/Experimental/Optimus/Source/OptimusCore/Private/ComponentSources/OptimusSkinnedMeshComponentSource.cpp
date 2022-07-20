// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusSkinnedMeshComponentSource.h"

#include "Components/SkinnedMeshComponent.h"


#define LOCTEXT_NAMESPACE "OptimusSkinnedMeshComponentSource"


FName UOptimusSkinnedMeshComponentSource::Contexts::Vertex("Vertex");
FName UOptimusSkinnedMeshComponentSource::Contexts::Triangle("Triangle");
FName UOptimusSkinnedMeshComponentSource::Contexts::Bone("Bone");


FText UOptimusSkinnedMeshComponentSource::GetDisplayName() const
{
	return LOCTEXT("SkinnedMeshComponent", "Skinned Mesh Component");
}

TSubclassOf<UActorComponent> UOptimusSkinnedMeshComponentSource::GetComponentClass() const
{
	return USkinnedMeshComponent::StaticClass();
}


TArray<FName> UOptimusSkinnedMeshComponentSource::GetExecutionContexts() const
{
	return {Contexts::Vertex, Contexts::Triangle, Contexts::Bone};
}


#undef LOCTEXT_NAMESPACE
