// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteDisplacedMeshComponent.h"

UNaniteDisplacedMeshComponent::UNaniteDisplacedMeshComponent(const FObjectInitializer& Init)
: Super(Init)
{
}

void UNaniteDisplacedMeshComponent::OnRegister()
{
	Super::OnRegister();
}

void UNaniteDisplacedMeshComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

const Nanite::FResources* UNaniteDisplacedMeshComponent::GetNaniteResources() const
{
	// TODO: Refactor API to support also overriding the mesh section info

	if (IsValid(DisplacedMesh) && DisplacedMesh->HasValidNaniteData())
	{
		return DisplacedMesh->GetNaniteData();
	}

	// If the displaced mesh does not have valid Nanite data, try the SMC's static mesh.
	if (GetStaticMesh() && GetStaticMesh()->GetRenderData())
	{
		return &GetStaticMesh()->GetRenderData()->NaniteResources;
	}

	return nullptr;
}

FPrimitiveSceneProxy* UNaniteDisplacedMeshComponent::CreateSceneProxy()
{
	return Super::CreateSceneProxy();
}

#if WITH_EDITOR

void UNaniteDisplacedMeshComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UNaniteDisplacedMeshComponent::PostEditUndo()
{
	Super::PostEditUndo();
}

#endif
