// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGPartitionActor.h"
#include "PCGComponent.h"
#include "PCGHelpers.h"
#include "PCGWorldActor.h"

#include "Landscape.h"
#include "Components/BoxComponent.h"

constexpr uint32 InvalidPCGGridSizeValue = 0u;

APCGPartitionActor::APCGPartitionActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PCGGridSize = InvalidPCGGridSizeValue;

#if WITH_EDITOR
	// Setup bounds component
	BoundsComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("BoundsComponent"));
	BoundsComponent->SetCollisionObjectType(ECC_WorldStatic);
	BoundsComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	BoundsComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoundsComponent->SetGenerateOverlapEvents(false);
	BoundsComponent->SetupAttachment(GetRootComponent());
	BoundsComponent->bDrawOnlyIfSelected = true;
#endif // WITH_EDITOR
}

void APCGPartitionActor::PostLoad()
{
	Super::PostLoad();

	// If the grid size is not set, set it to the default value.
	if (PCGGridSize == InvalidPCGGridSizeValue)
	{
		PCGGridSize = APCGWorldActor::DefaultPartitionGridSize;
	}

#if WITH_EDITOR
	// Make sure that we don't track objects that do not exist anymore
	CleanupDeadGraphInstances();

	if (BoundsComponent)
	{
		BoundsComponent->SetBoxExtent(GetFixedBounds().GetExtent());
	}
#endif // WITH_EDITOR
}

void APCGPartitionActor::BeginPlay()
{
	// Pass through all the pcg components, and make sure we match generation trigger for the local component if it is not overriden
	for (auto& It : OriginalToLocalMap)
	{
		if (It.Key && It.Value && !It.Value->bGenerationTriggerLocalOverride)
		{
			It.Value->GenerationTrigger = It.Key->GenerationTrigger;
		}
	}

	Super::BeginPlay();

	// Register cell to the PCG grid
	// TODO
}

void APCGPartitionActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Unregister each cell to the PCG grid
	// TODO

	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
uint32 APCGPartitionActor::GetDefaultGridSize(UWorld* InWorld) const
{
	if (APCGWorldActor* PCGActor = PCGHelpers::GetPCGWorldActor(InWorld))
	{
		return PCGActor->PartitionGridSize;
	}

	UE_LOG(LogPCG, Error, TEXT("[APCGPartitionActor::InternalGetDefaultGridSize] PCG World Actor was null. Returning default value"));
	return APCGWorldActor::DefaultPartitionGridSize;
}
#endif

FBox APCGPartitionActor::GetFixedBounds() const
{
	const FVector Center = GetActorLocation();
	const FVector::FReal HalfGridSize = PCGGridSize / 2.0;
	return FBox(Center - HalfGridSize, Center + HalfGridSize);
}

void APCGPartitionActor::GetActorBounds(bool bOnlyCollidingComponents, FVector& Origin, FVector& BoxExtent, bool bIncludeFromChildActors) const
{
	Super::GetActorBounds(bOnlyCollidingComponents, Origin, BoxExtent, bIncludeFromChildActors);

	// To keep consistency with the other GetBounds functions, transform our result into an origin / extent formatting
	FBox Bounds(Origin - BoxExtent, Origin + BoxExtent);
	Bounds += GetFixedBounds();
	Bounds.GetCenterAndExtents(Origin, BoxExtent);
}

UPCGComponent* APCGPartitionActor::GetLocalComponent(const UPCGComponent* OriginalComponent) const
{
	const TObjectPtr<UPCGComponent>* LocalComponent = OriginalToLocalMap.Find(OriginalComponent);
	return LocalComponent ? *LocalComponent : nullptr;
}

UPCGComponent* APCGPartitionActor::GetOriginalComponent(const UPCGComponent* LocalComponent) const
{
	const TObjectPtr<UPCGComponent>* OriginalComponent = LocalToOriginalMap.Find(LocalComponent);
	return OriginalComponent ? (*OriginalComponent).Get() : nullptr;
}

#if WITH_EDITOR
FBox APCGPartitionActor::GetStreamingBounds() const
{
	return Super::GetStreamingBounds() + GetFixedBounds();
}

AActor* APCGPartitionActor::GetSceneOutlinerParent() const
{
	if (APCGWorldActor* PCGActor = PCGHelpers::GetPCGWorldActor(GetWorld()))
	{
		return PCGActor;
	}
	else
	{
		return Super::GetSceneOutlinerParent();
	}	
}

void APCGPartitionActor::AddGraphInstance(UPCGComponent* OriginalComponent)
{
	if (!OriginalComponent)
	{
		return;
	}

	// Make sure we don't have that graph twice;
	// Here we'll check if there has been some changes worth propagating or not
	UPCGComponent* LocalComponent = GetLocalComponent(OriginalComponent);

	if (LocalComponent)
	{
		// Update properties as needed and early out
		LocalComponent->SetPropertiesFromOriginal(OriginalComponent);
		return;
	}

	Modify();

	// Create a new local component
	LocalComponent = NewObject<UPCGComponent>(this);
	LocalComponent->SetPropertiesFromOriginal(OriginalComponent);

	LocalComponent->RegisterComponent();
	// TODO: check if we should use a non-instanced component?
	AddInstanceComponent(LocalComponent);

	OriginalToLocalMap.Add(OriginalComponent, LocalComponent);
	LocalToOriginalMap.Add(LocalComponent, OriginalComponent);
}

bool APCGPartitionActor::RemoveGraphInstance(UPCGComponent* OriginalComponent)
{
	UPCGComponent* LocalComponent = GetLocalComponent(OriginalComponent);

	if (!LocalComponent)
	{
		return false;
	}

	Modify();

	OriginalToLocalMap.Remove(OriginalComponent);
	LocalToOriginalMap.Remove(LocalComponent);

	// TODO Add option to not cleanup?
	LocalComponent->CleanupLocal(/*bRemoveComponents=*/true);
	LocalComponent->DestroyComponent();

	return OriginalToLocalMap.IsEmpty();
}

bool APCGPartitionActor::CleanupDeadGraphInstances()
{
	TSet<TObjectPtr<UPCGComponent>> DeadLocalInstances;

	// Note: since we might end up with multiple nulls in the original to local map
	// it might not be very stable to use it; we'll use the 
	for (const auto& LocalToOriginalItem : LocalToOriginalMap)
	{
		if (!LocalToOriginalItem.Value)
		{
			DeadLocalInstances.Add(LocalToOriginalItem.Key);
		}
	}

	if (DeadLocalInstances.Num() == 0)
	{
		return OriginalToLocalMap.IsEmpty();
	}

	Modify();

	for (const TObjectPtr<UPCGComponent>& DeadInstance : DeadLocalInstances)
	{
		LocalToOriginalMap.Remove(DeadInstance);

		if (DeadInstance)
		{
			DeadInstance->CleanupLocal(/*bRemoveComponents=*/true);
			DeadInstance->DestroyComponent();
		}
	}

	// Remove all dead entries
	OriginalToLocalMap.Remove(nullptr);

	return OriginalToLocalMap.IsEmpty();
}

void APCGPartitionActor::PostCreation()
{
	PCGGridSize = GridSize;

#if WITH_EDITOR
	if (BoundsComponent)
	{
		BoundsComponent->SetBoxExtent(GetFixedBounds().GetExtent());
	}
#endif // WITH_EDITOR
}

bool APCGPartitionActor::IsSafeForDeletion() const
{
	ensure(IsInGameThread());
	for (TObjectPtr<UPCGComponent> PCGComponent : GetAllOriginalPCGComponents())
	{
		if (PCGComponent && PCGComponent->IsGenerating())
		{
			return false;
		}
	}

	return true;
}

TSet<TObjectPtr<UPCGComponent>> APCGPartitionActor::GetAllLocalPCGComponents() const
{
	TSet<TObjectPtr<UPCGComponent>> ResultComponents;
	LocalToOriginalMap.GetKeys(ResultComponents);

	return ResultComponents;
}

TSet<TObjectPtr<UPCGComponent>> APCGPartitionActor::GetAllOriginalPCGComponents() const
{
	TSet<TObjectPtr<UPCGComponent>> ResultComponents;
	OriginalToLocalMap.GetKeys(ResultComponents);

	return ResultComponents;
}

#endif // WITH_EDITOR