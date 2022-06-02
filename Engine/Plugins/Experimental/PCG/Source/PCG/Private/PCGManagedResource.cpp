// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGManagedResource.h"
#include "PCGComponent.h"

#include "Components/InstancedStaticMeshComponent.h"

void UPCGManagedActors::PostEditImport()
{
	// In this case, the managed actors won't be copied along the actor/component,
	// So we just have to "forget" the actors.
	Super::PostEditImport();
	GeneratedActors.Reset();
}

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

void UPCGManagedComponent::PostEditImport()
{
	Super::PostEditImport();

	// Rehook components from the original to the locally duplicated components
	UPCGComponent* OwningComponent = Cast<UPCGComponent>(GetOuter());
	AActor* Actor = OwningComponent ? OwningComponent->GetOwner() : nullptr;

	bool bFoundMatch = false;

	if (Actor && GeneratedComponent.IsValid())
	{
		TInlineComponentArray<UActorComponent*, 16> Components;
		Actor->GetComponents(Components);

		for (UActorComponent* Component : Components)
		{
			if (Component && Component->GetFName() == GeneratedComponent->GetFName())
			{
				GeneratedComponent = Component;
				bFoundMatch = true;
			}
		}

		if (!bFoundMatch)
		{
			// Not quite clear what to do when we have a component that cannot be remapped.
			// Maybe we should check against guids instead?
			GeneratedComponent.Reset();
		}
	}
	else
	{
		// Somewhat irrelevant case, if we don't have an actor or a component, there's not a lot we can do.
		GeneratedComponent.Reset();
	}
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