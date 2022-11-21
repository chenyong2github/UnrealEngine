// Copyright Epic Games, Inc. All Rights Reserved.

#include "Helpers/PCGBlueprintHelpers.h"
#include "LandscapeComponent.h"
#include "PCGComponent.h"
#include "PCGHelpers.h"
#include "PCGSettings.h"
#include "PCGSubsystem.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Grid/PCGPartitionActor.h"
#include "Grid/PCGLandscapeCache.h"
#include "Engine/World.h"
#include "UObject/UObjectIterator.h"
#include "LandscapeProxy.h"
#include "LandscapeInfo.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGBlueprintHelpers)

int UPCGBlueprintHelpers::ComputeSeedFromPosition(const FVector& InPosition)
{
	// TODO: should have a config to drive this
	return PCGHelpers::ComputeSeed((int)InPosition.X, (int)InPosition.Y, (int)InPosition.Z);
}

void UPCGBlueprintHelpers::SetSeedFromPosition(FPCGPoint& InPoint)
{
	InPoint.Seed = ComputeSeedFromPosition(InPoint.Transform.GetLocation());
}

FRandomStream UPCGBlueprintHelpers::GetRandomStream(const FPCGPoint& InPoint, const UPCGSettings* OptionalSettings, const UPCGComponent* OptionalComponent)
{
	int Seed = InPoint.Seed;

	if (OptionalSettings && OptionalComponent)
	{
		Seed = PCGHelpers::ComputeSeed(InPoint.Seed, OptionalSettings->Seed, OptionalComponent->Seed);
	}
	else if (OptionalSettings)
	{
		Seed = PCGHelpers::ComputeSeed(InPoint.Seed, OptionalSettings->Seed);
	}
	else if (OptionalComponent)
	{
		Seed = PCGHelpers::ComputeSeed(InPoint.Seed, OptionalComponent->Seed);
	}

	return FRandomStream(Seed);
}

const UPCGSettings* UPCGBlueprintHelpers::GetSettings(FPCGContext& Context)
{
	return Context.GetInputSettings<UPCGSettings>();
}

UPCGData* UPCGBlueprintHelpers::GetActorData(FPCGContext& Context)
{
	return Context.SourceComponent.IsValid() ? Context.SourceComponent->GetActorPCGData() : nullptr;
}

UPCGData* UPCGBlueprintHelpers::GetInputData(FPCGContext& Context)
{
	return Context.SourceComponent.IsValid() ? Context.SourceComponent->GetInputPCGData() : nullptr;
}

TArray<UPCGData*> UPCGBlueprintHelpers::GetExclusionData(FPCGContext& Context)
{
	return Context.SourceComponent.IsValid() ? Context.SourceComponent->GetPCGExclusionData() : TArray<UPCGData*>();
}

UPCGComponent* UPCGBlueprintHelpers::GetComponent(FPCGContext& Context)
{
	return Context.SourceComponent.Get();
}

UPCGComponent* UPCGBlueprintHelpers::GetOriginalComponent(FPCGContext& Context)
{
	if (Context.SourceComponent.IsValid() &&
		Cast<APCGPartitionActor>(Context.SourceComponent->GetOwner()) &&
		Cast<APCGPartitionActor>(Context.SourceComponent->GetOwner())->GetOriginalComponent(Context.SourceComponent.Get()))
	{
		return Cast<APCGPartitionActor>(Context.SourceComponent->GetOwner())->GetOriginalComponent(Context.SourceComponent.Get());
	}
	else
	{
		return Context.SourceComponent.Get();
	}
}

void UPCGBlueprintHelpers::SetExtents(FPCGPoint& InPoint, const FVector& InExtents)
{
	InPoint.SetExtents(InExtents);
}

FVector UPCGBlueprintHelpers::GetExtents(const FPCGPoint& InPoint)
{
	return InPoint.GetExtents();
}

void UPCGBlueprintHelpers::SetLocalCenter(FPCGPoint& InPoint, const FVector& InLocalCenter)
{
	InPoint.SetLocalCenter(InLocalCenter);
}

FVector UPCGBlueprintHelpers::GetLocalCenter(const FPCGPoint& InPoint)
{
	return InPoint.GetLocalCenter();
}

FBox UPCGBlueprintHelpers::GetTransformedBounds(const FPCGPoint& InPoint)
{
	return FBox(InPoint.BoundsMin, InPoint.BoundsMax).TransformBy(InPoint.Transform);
}

FBox UPCGBlueprintHelpers::GetActorBoundsPCG(AActor* InActor, bool bIgnorePCGCreatedComponents)
{
	return PCGHelpers::GetActorBounds(InActor, bIgnorePCGCreatedComponents);
}

FBox UPCGBlueprintHelpers::GetActorLocalBoundsPCG(AActor* InActor, bool bIgnorePCGCreatedComponents)
{
	return PCGHelpers::GetActorLocalBounds(InActor, bIgnorePCGCreatedComponents);
}

UPCGData* UPCGBlueprintHelpers::CreatePCGDataFromActor(AActor* InActor, bool bParseActor)
{
	return UPCGComponent::CreateActorPCGData(InActor, nullptr, bParseActor);
}

TArray<FPCGLandscapeLayerWeight> UPCGBlueprintHelpers::GetInterpolatedPCGLandscapeLayerWeights(UObject* WorldContextObject, const FVector& Location)
{
	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World)
		return {};

	UPCGSubsystem* PCGSubSystem = UWorld::GetSubsystem<UPCGSubsystem>(World);
	if (!PCGSubSystem)
		return {};

	FIntPoint ComponentKey{};

	auto FindLandscapeComponent = [World, Location, &ComponentKey]() -> ULandscapeComponent* {
		for (TObjectIterator<ALandscapeProxy> It; It; ++It)
		{
			if (It->GetWorld() == World)
			{
				FBox Box = It->GetComponentsBoundingBox();
				if (Box.IsInsideOrOnXY(Location))
				{
					if (ULandscapeInfo* Info = It->GetLandscapeInfo())
					{
						const FVector ActorSpaceLocation = It->LandscapeActorToWorld().InverseTransformPosition(Location);
						ComponentKey = FIntPoint(FMath::FloorToInt(ActorSpaceLocation.X / It->ComponentSizeQuads), FMath::FloorToInt(ActorSpaceLocation.Y / It->ComponentSizeQuads));

						return Info->XYtoComponentMap.FindRef(ComponentKey);
					}

					return nullptr;
				}
			}
		}

		return nullptr;
	};

	ULandscapeComponent* LandscapeComponent = FindLandscapeComponent();
	if (!LandscapeComponent)
		return {};

	const FVector ComponentSpaceLocation = LandscapeComponent->GetComponentToWorld().InverseTransformPosition(Location);

	UPCGLandscapeCache* LandscapeCache = PCGSubSystem->GetLandscapeCache();
	if (!LandscapeCache)
		return {};

	const FPCGLandscapeCacheEntry* CacheEntry = LandscapeCache->GetCacheEntry(LandscapeComponent, ComponentKey);
	if (!CacheEntry)
		return {};

	TArray<FPCGLandscapeLayerWeight> Result;

	CacheEntry->GetInterpolatedLayerWeights(FVector2D(ComponentSpaceLocation), Result);

	Result.Sort([](const FPCGLandscapeLayerWeight& Lhs, const FPCGLandscapeLayerWeight& Rhs){
		return Lhs.Weight > Rhs.Weight;
	});
					
	return Result;
}
