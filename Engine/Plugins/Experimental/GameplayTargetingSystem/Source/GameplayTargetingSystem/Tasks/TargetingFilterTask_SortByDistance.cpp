// Copyright Epic Games, Inc. All Rights Reserved.
#include "TargetingFilterTask_SortByDistance.h"

#include "GameplayTargetingSystem/TargetingSystem/TargetingSubsystem.h"

#if ENABLE_DRAW_DEBUG
#include "Engine/Canvas.h"
#endif // ENABLE_DRAW_DEBUG

namespace SortByDistanceConstants
{
	const FString PreSortPrefix = TEXT("PreSort");
	const FString PostSortPrefix = TEXT("PostSort");
}

UTargetingFilterTask_SortByDistance::UTargetingFilterTask_SortByDistance(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
	bAscending = true;
}

void UTargetingFilterTask_SortByDistance::Execute(const FTargetingRequestHandle& TargetingHandle) const
{
	Super::Execute(TargetingHandle);

	SetTaskAsyncState(TargetingHandle, ETargetingTaskAsyncState::Executing);

#if ENABLE_DRAW_DEBUG
	ResetSortDebugStrings(TargetingHandle);
#endif // ENABLE_DRAW_DEBUG

	if (TargetingHandle.IsValid())
	{
		FVector SourceLocation = FVector::ZeroVector;
		if (FTargetingSourceContext* SourceContext = FTargetingSourceContext::Find(TargetingHandle))
		{
			if (SourceContext->SourceActor)
			{
				SourceLocation = SourceContext->SourceActor->GetActorLocation();
			}
			else if (!SourceContext->SourceLocation.IsZero())
			{
				SourceLocation = SourceContext->SourceLocation;
			}
		}

		if (!SourceLocation.IsZero())
		{
			if (FTargetingDefaultResultsSet* ResultData = FTargetingDefaultResultsSet::Find(TargetingHandle))
			{
#if ENABLE_DRAW_DEBUG
				BuildPreSortDebugString(TargetingHandle, ResultData->TargetResults);
#endif // ENABLE_DRAW_DEBUG

				// compute the distances
				const int32 NumTargets = ResultData->TargetResults.Num();
				for (int32 TargetIterator = NumTargets - 1; TargetIterator >= 0; --TargetIterator)
				{
					FTargetingDefaultResultData& TargetResult = ResultData->TargetResults[TargetIterator];
					if (AActor* TargetActor = TargetResult.HitResult.GetActor())
					{
						FVector TargetLocation = TargetActor->GetActorLocation();
						TargetResult.Score = FVector::DistSquared(SourceLocation, TargetLocation);
					}
				}

				// sort the set
				ResultData->TargetResults.Sort([this](const FTargetingDefaultResultData& Lhs, const FTargetingDefaultResultData& Rhs)
					{
						if (bAscending)
						{
							return Lhs.Score < Rhs.Score;
						}
						else
						{
							return Lhs.Score > Rhs.Score;
						}
					});

#if ENABLE_DRAW_DEBUG
				BuildPostSortDebugString(TargetingHandle, ResultData->TargetResults);
#endif // ENABLE_DRAW_DEBUG
			}
		}
	}

	SetTaskAsyncState(TargetingHandle, ETargetingTaskAsyncState::Completed);
}

#if ENABLE_DRAW_DEBUG

void UTargetingFilterTask_SortByDistance::DrawDebug(UTargetingSubsystem* TargetingSubsystem, FTargetingDebugInfo& Info, const FTargetingRequestHandle& TargetingHandle, float XOffset, float YOffset, int32 MinTextRowsToAdvance) const
{
#if WITH_EDITORONLY_DATA
	if (UTargetingSubsystem::IsTargetingDebugEnabled())
	{
		FTargetingDebugData& DebugData = FTargetingDebugData::FindOrAdd(TargetingHandle);
		const FString& PreSortScratchPadString = DebugData.DebugScratchPadStrings.FindOrAdd(SortByDistanceConstants::PreSortPrefix + GetNameSafe(this));
		const FString& PostSortScratchPadString = DebugData.DebugScratchPadStrings.FindOrAdd(SortByDistanceConstants::PostSortPrefix + GetNameSafe(this));
		if (!PreSortScratchPadString.IsEmpty() && !PostSortScratchPadString.IsEmpty())
		{
			if (Info.Canvas)
			{
				Info.Canvas->SetDrawColor(FColor::Yellow);
			}

			FString TaskString = FString::Printf(TEXT("Initial : %s"), *PreSortScratchPadString);
			TargetingSubsystem->DebugLine(Info, TaskString, XOffset, YOffset, MinTextRowsToAdvance);

			TaskString = FString::Printf(TEXT("Sorted : %s"), *PostSortScratchPadString);
			TargetingSubsystem->DebugLine(Info, TaskString, XOffset, YOffset, MinTextRowsToAdvance);
		}
	}
#endif // WITH_EDITORONLY_DATA
}

void UTargetingFilterTask_SortByDistance::BuildPreSortDebugString(const FTargetingRequestHandle& TargetingHandle, const TArray<FTargetingDefaultResultData>& TargetResults) const
{
#if WITH_EDITORONLY_DATA
	if (UTargetingSubsystem::IsTargetingDebugEnabled())
	{
		FTargetingDebugData& DebugData = FTargetingDebugData::FindOrAdd(TargetingHandle);
		FString& PreSortScratchPadString = DebugData.DebugScratchPadStrings.FindOrAdd(SortByDistanceConstants::PreSortPrefix + GetNameSafe(this));

		for (const FTargetingDefaultResultData& TargetData : TargetResults)
		{
			if (const AActor* Target = TargetData.HitResult.GetActor())
			{
				if (PreSortScratchPadString.IsEmpty())
				{
					PreSortScratchPadString = FString::Printf(TEXT("%s"), *GetNameSafe(Target));
				}
				else
				{
					PreSortScratchPadString += FString::Printf(TEXT(", %s"), *GetNameSafe(Target));
				}
			}
		}
	}
#endif // WITH_EDITORONLY_DATA
}

void UTargetingFilterTask_SortByDistance::BuildPostSortDebugString(const FTargetingRequestHandle& TargetingHandle, const TArray<FTargetingDefaultResultData>& TargetResults) const
{
#if WITH_EDITORONLY_DATA
	if (UTargetingSubsystem::IsTargetingDebugEnabled())
	{
		FTargetingDebugData& DebugData = FTargetingDebugData::FindOrAdd(TargetingHandle);
		FString& PostSortScratchPadString = DebugData.DebugScratchPadStrings.FindOrAdd(SortByDistanceConstants::PostSortPrefix + GetNameSafe(this));

		for (const FTargetingDefaultResultData& TargetData : TargetResults)
		{
			if (const AActor* Target = TargetData.HitResult.GetActor())
			{
				if (PostSortScratchPadString.IsEmpty())
				{
					PostSortScratchPadString = FString::Printf(TEXT("%s"), *GetNameSafe(Target));
				}
				else
				{
					PostSortScratchPadString += FString::Printf(TEXT(", %s"), *GetNameSafe(Target));
				}
			}
		}
	}
#endif // WITH_EDITORONLY_DATA
}

void UTargetingFilterTask_SortByDistance::ResetSortDebugStrings(const FTargetingRequestHandle& TargetingHandle) const
{
#if WITH_EDITORONLY_DATA
	FTargetingDebugData& DebugData = FTargetingDebugData::FindOrAdd(TargetingHandle);
	FString& PreSortScratchPadString = DebugData.DebugScratchPadStrings.FindOrAdd(SortByDistanceConstants::PreSortPrefix + GetNameSafe(this));
	PreSortScratchPadString.Reset();

	FString& PostSortScratchPadString = DebugData.DebugScratchPadStrings.FindOrAdd(SortByDistanceConstants::PostSortPrefix + GetNameSafe(this));
	PostSortScratchPadString.Reset();
#endif // WITH_EDITORONLY_DATA
}

#endif // ENABLE_DRAW_DEBUG