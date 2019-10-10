// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MagicLeapPlanesFunctionLibrary.h"
#include "MagicLeapPlanesModule.h"

bool UMagicLeapPlanesFunctionLibrary::CreateTracker()
{
	return GetMagicLeapPlanesModule().CreateTracker();
}

bool UMagicLeapPlanesFunctionLibrary::DestroyTracker()
{
	return GetMagicLeapPlanesModule().DestroyTracker();
}

bool UMagicLeapPlanesFunctionLibrary::IsTrackerValid()
{
	return GetMagicLeapPlanesModule().IsTrackerValid();
}

bool UMagicLeapPlanesFunctionLibrary::PlanesQueryBeginAsync(const FMagicLeapPlanesQuery& InQuery, const FMagicLeapPlanesResultDelegate& InResultDelegate)
{
	FMagicLeapPlanesResultDelegateMulti ResultDelegate;
	ResultDelegate.Add(InResultDelegate);

	return GetMagicLeapPlanesModule().QueryBeginAsync(
		InQuery,
		ResultDelegate);
}

FTransform UMagicLeapPlanesFunctionLibrary::GetContentScale(const AActor* ContentActor, const FMagicLeapPlaneResult& PlaneResult)
{
	check(ContentActor != nullptr);

	const FBox ContentActorBox = ContentActor->GetComponentsBoundingBox(true);

	const FVector ContentActorScale = ContentActor->GetActorScale3D();
	const FVector OriginalContentActorDimensions = (ContentActorBox.Max - ContentActor->GetActorLocation()) / ContentActorScale;

	FVector FinalContentActorDimensions = FVector::ZeroVector;

	const FVector2D PlaneDimensions = PlaneResult.PlaneDimensions / 2;

	// For Unreal, Y = Right (Width)
	FinalContentActorDimensions.Y = PlaneDimensions.Y;
	FinalContentActorDimensions.X = FinalContentActorDimensions.Y * OriginalContentActorDimensions.X / OriginalContentActorDimensions.Y;
	if (FinalContentActorDimensions.X > PlaneDimensions.X)
	{
		FinalContentActorDimensions.Y = FinalContentActorDimensions.Y * PlaneDimensions.X / FinalContentActorDimensions.X;
		FinalContentActorDimensions.X = PlaneDimensions.X;
	}
	FinalContentActorDimensions.Z = OriginalContentActorDimensions.Z * FinalContentActorDimensions.X / OriginalContentActorDimensions.X;

	const FVector FinalContentActorScale = FinalContentActorDimensions / OriginalContentActorDimensions;

	return FTransform(PlaneResult.PlaneOrientation, PlaneResult.PlanePosition, FinalContentActorScale);
}

void UMagicLeapPlanesFunctionLibrary::ReorderPlaneFlags(const TArray<EMagicLeapPlaneQueryFlags>& InPriority, const TArray<EMagicLeapPlaneQueryFlags>& InFlagsToReorder, TArray<EMagicLeapPlaneQueryFlags>& OutReorderedFlags)
{
	struct FPlaneFlagPredicate
	{
		FPlaneFlagPredicate(const TArray<EMagicLeapPlaneQueryFlags>& Priority)
		: PriorityArray(&Priority)
		{}

		bool operator()(const EMagicLeapPlaneQueryFlags& FlagA, const EMagicLeapPlaneQueryFlags& FlagB) const
		{
			return PriorityArray->Find(FlagA) > PriorityArray->Find(FlagB);
		}
	
	private:
		const TArray<EMagicLeapPlaneQueryFlags>* PriorityArray;
	};

	OutReorderedFlags = InFlagsToReorder;
	OutReorderedFlags.HeapSort(FPlaneFlagPredicate(InPriority));
}

void UMagicLeapPlanesFunctionLibrary::RemoveFlagsNotInQuery(const TArray<EMagicLeapPlaneQueryFlags>& InQueryFlags, const TArray<EMagicLeapPlaneQueryFlags>& InResultFlags, TArray<EMagicLeapPlaneQueryFlags>& OutFlags)
{
	for (int32 i = 0; i < InResultFlags.Num(); ++i)
	{
		if (InQueryFlags.Find(InResultFlags[i]) >= 0)
		{
			OutFlags.Add(InResultFlags[i]);
		}
	}
}
