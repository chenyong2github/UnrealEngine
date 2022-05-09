// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteDisplacedMeshComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogNaniteDisplacedMesh, Log, All);

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
	// TODO: Remap to UNaniteDisplacedMesh NaniteResources
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
