// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusSkeletalMeshComponentSource.h"

#include "Components/SkeletalMeshComponent.h"


#define LOCTEXT_NAMESPACE "OptimusSkeletalMeshComponentSource"


FName UOptimusSkeletalMeshComponentSource::Contexts::Vertex("Vertex");
FName UOptimusSkeletalMeshComponentSource::Contexts::Triangle("Triangle");
FName UOptimusSkeletalMeshComponentSource::Contexts::Bone("Bone");


TSubclassOf<UActorComponent> UOptimusSkeletalMeshComponentSource::GetComponentClass() const
{
	return USkeletalMeshComponent::StaticClass();
}


FText UOptimusSkeletalMeshComponentSource::GetDisplayName() const
{
	return LOCTEXT("SkeletalMeshComponent", "Skeletal Mesh Component");
}

TArray<FName> UOptimusSkeletalMeshComponentSource::GetExecutionContexts() const
{
	return {Contexts::Vertex, Contexts::Triangle, Contexts::Bone};
}


#undef LOCTEXT_NAMESPACE
