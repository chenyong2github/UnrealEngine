// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGHelpers.h"

#include "PCGSubsystem.h"
#include "PCGWorldActor.h"

#include "GameFramework/Actor.h"
#include "Landscape.h"
#include "UObject/UObjectIterator.h"
#include "WorldPartition/WorldPartition.h"

namespace PCGHelpers
{
	int ComputeSeed(int A)
	{
		return (A * 196314165U) + 907633515U;
	}

	int ComputeSeed(int A, int B)
	{
		return ((A * 196314165U) + 907633515U) ^ ((B * 73148459U) + 453816763U);
	}

	int ComputeSeed(int A, int B, int C)
	{
		return ((A * 196314165U) + 907633515U) ^ ((B * 73148459U) + 453816763U) ^ ((C * 34731343U) + 453816743U);
	}

	FBox GetActorBounds(AActor* InActor)
	{
		// Specialized version of GetComponentsBoundingBox that skips over PCG generated components
		// This is to ensure stable bounds and no timing issues (cleared ISMs, etc.)
		FBox Box(EForceInit::ForceInit);

		const bool bNonColliding = true;
		const bool bIncludeFromChildActors = true;

		InActor->ForEachComponent<UPrimitiveComponent>(bIncludeFromChildActors, [&](const UPrimitiveComponent* InPrimComp)
			{
				// Note: we omit the IsRegistered check here (e.g. InPrimComp->IsRegistered() )
				// since this can be called in a scope where the components are temporarily unregistered
				if ((bNonColliding || InPrimComp->IsCollisionEnabled()) &&
					!InPrimComp->ComponentTags.Contains(DefaultPCGTag))
				{
					Box += InPrimComp->Bounds.GetBox();
				}
			});

		return Box;
	}

	FBox GetLandscapeBounds(ALandscapeProxy* InLandscape)
	{
		check(InLandscape);

		if (ALandscape* Landscape = Cast<ALandscape>(InLandscape))
		{
#if WITH_EDITOR
			return Landscape->GetCompleteBounds();
#else
			return Landscape->GetLoadedBounds();
#endif
		}
		else
		{
			return GetActorBounds(InLandscape);
		}
	}

	ALandscape* GetLandscape(UWorld* InWorld, const FBox& InBounds)
	{
		ALandscape* Landscape = nullptr;

		if (!InBounds.IsValid)
		{
			return Landscape;
		}

		for (TObjectIterator<ALandscape> It; It; ++It)
		{
			if (It->GetWorld() == InWorld)
			{
				const FBox LandscapeBounds = GetLandscapeBounds(*It);
				if (LandscapeBounds.IsValid && LandscapeBounds.Intersect(InBounds))
				{
					Landscape = (*It);
					break;
				}
			}
		}

		return Landscape;
	}

#if WITH_EDITOR
	APCGWorldActor* GetPCGWorldActor(UWorld* InWorld)
	{
		return InWorld && InWorld->GetSubsystem<UPCGSubsystem>() ? InWorld->GetSubsystem<UPCGSubsystem>()->GetPCGWorldActor() : nullptr;
	}
#endif
}
