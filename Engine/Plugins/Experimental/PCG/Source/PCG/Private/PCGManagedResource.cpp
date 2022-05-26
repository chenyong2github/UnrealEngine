// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGManagedResource.h"
#include "PCGComponent.h"

#include "Components/InstancedStaticMeshComponent.h"

bool UPCGManagedActors::Release(bool bHardRelease, TSet<TSoftObjectPtr<AActor>>& OutActorsToDelete)
{
	OutActorsToDelete.Append(GeneratedActors);

	// Cleanup recursively
	TInlineComponentArray<UPCGComponent*, 1> ComponentsToCleanup;

	for (TSoftObjectPtr<AActor> GeneratedActor : GeneratedActors)
	{
		if (GeneratedActor.IsValid())
		{
			GeneratedActor.Get()->GetComponents<UPCGComponent>(ComponentsToCleanup);

			for (UPCGComponent* Component : ComponentsToCleanup)
			{
				Component->CleanupInternal(/*bRemoveComponents=*/false, OutActorsToDelete);
			}

			ComponentsToCleanup.Reset();
		}
	}

	GeneratedActors.Reset();
	return true;
}

bool UPCGManagedActors::ReleaseIfUnused()
{
	return GeneratedActors.IsEmpty();
}

bool UPCGManagedComponent::Release(bool bHardRelease, TSet<TSoftObjectPtr<AActor>>& OutActorsToDelete)
{
	const bool bSupportsComponentReset = SupportsComponentReset();
	const bool bDeleteComponent = bHardRelease || !bSupportsComponentReset;

	if (GeneratedComponent.IsValid())
	{
		if (bDeleteComponent)
		{
			GeneratedComponent->DestroyComponent();
		}
		else
		{
			ResetComponent();
		}
	}

	return bDeleteComponent;
}

bool UPCGManagedComponent::ReleaseIfUnused()
{
	return !GeneratedComponent.IsValid();
}

bool UPCGManagedISMComponent::ReleaseIfUnused()
{
	if (Super::ReleaseIfUnused() || !GetComponent())
	{
		return true;
	}
	else if (GetComponent()->GetInstanceCount() == 0)
	{
		GeneratedComponent->DestroyComponent();
		return true;
	}
	else
	{
		return false;
	}
}

void UPCGManagedISMComponent::ResetComponent()
{
	if (UInstancedStaticMeshComponent * ISMC = GetComponent())
	{
		ISMC->ClearInstances();
		ISMC->UpdateBounds();
	}
}

UInstancedStaticMeshComponent* UPCGManagedISMComponent::GetComponent() const
{
	return Cast<UInstancedStaticMeshComponent>(GeneratedComponent.Get());
}