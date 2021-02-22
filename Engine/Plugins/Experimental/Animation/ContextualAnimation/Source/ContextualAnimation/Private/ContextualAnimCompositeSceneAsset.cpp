// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimCompositeSceneAsset.h"
#include "ContextualAnimMetadata.h"
#include "Animation/AnimMontage.h"

const FName UContextualAnimCompositeSceneAsset::InteractorRoleName = FName(TEXT("interactor"));
const FName UContextualAnimCompositeSceneAsset::InteractableRoleName = FName(TEXT("interactable"));

UContextualAnimCompositeSceneAsset::UContextualAnimCompositeSceneAsset(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryRole = UContextualAnimCompositeSceneAsset::InteractableRoleName;
}

UClass* UContextualAnimCompositeSceneAsset::GetPreviewActorClassForRole(const FName& Role) const
{
	return Role == PrimaryRole ? InteractableTrack.Settings.PreviewActorClass : InteractorTrack.Settings.PreviewActorClass;
}

EContextualAnimJoinRule UContextualAnimCompositeSceneAsset::GetJoinRuleForRole(const FName& Role) const
{
	return Role == PrimaryRole ? InteractableTrack.Settings.JoinRule : InteractorTrack.Settings.JoinRule;
}

void UContextualAnimCompositeSceneAsset::PreSave(const class ITargetPlatform* TargetPlatform)
{
	Super::PreSave(TargetPlatform);

	for (FContextualAnimData& Data : InteractorTrack.AnimDataContainer)
	{
		GenerateAlignmentTracksRelativeToScenePivot(Data);
	}
}

bool UContextualAnimCompositeSceneAsset::QueryData(FContextualAnimQueryResult& OutResult, const FContextualAnimQueryParams& QueryParams, const FTransform& ToWorldTransform) const
{
	OutResult.Reset();

	const FTransform QueryTransform = QueryParams.Querier.IsValid() ? QueryParams.Querier->GetActorTransform() : QueryParams.QueryTransform;

	int32 DataIndex = INDEX_NONE;
	if (QueryParams.bComplexQuery)
	{
		for (int32 Idx = 0; Idx < InteractorTrack.AnimDataContainer.Num(); Idx++)
		{
			const FContextualAnimData& Data = InteractorTrack.AnimDataContainer[Idx];

			if (Data.Metadata)
			{
				const FTransform EntryTransform = Data.GetAlignmentTransformAtEntryTime() * ToWorldTransform;

				FVector Origin = ToWorldTransform.GetLocation();
				FVector DirToEntry = (EntryTransform.GetLocation() - Origin).GetSafeNormal2D();

				if (Data.Metadata->OffsetFromOrigin != 0.f)
				{
					Origin = Origin + DirToEntry * Data.Metadata->OffsetFromOrigin;
				}

				// Distance Test
				//--------------------------------------------------
				if (Data.Metadata->Distance.MaxDistance > 0.f || Data.Metadata->Distance.MinDistance > 0.f)
				{
					const float DistSq = FVector::DistSquared2D(Origin, QueryTransform.GetLocation());

					if (Data.Metadata->Distance.MaxDistance > 0.f)
					{
						if (DistSq > FMath::Square(Data.Metadata->Distance.MaxDistance))
						{
							continue;
						}
					}

					if (Data.Metadata->Distance.MinDistance > 0.f)
					{
						if (DistSq < FMath::Square(Data.Metadata->Distance.MinDistance))
						{
							continue;
						}
					}
				}

				// Angle Test
				//--------------------------------------------------
				if (Data.Metadata->Angle.Tolerance > 0.f)
				{
					//@TODO: Cache this
					const float AngleCos = FMath::Cos(FMath::Clamp(FMath::DegreesToRadians(Data.Metadata->Angle.Tolerance), 0.f, PI));
					const FVector ToTarget = (QueryTransform.GetLocation() - Origin).GetSafeNormal2D();
					if (FVector::DotProduct(ToTarget, DirToEntry) < AngleCos)
					{
						continue;
					}
				}

				// Facing Test
				//--------------------------------------------------
				if (Data.Metadata->Facing.Tolerance > 0.f)
				{
					//@TODO: Cache this
					const float FacingCos = FMath::Cos(FMath::Clamp(FMath::DegreesToRadians(Data.Metadata->Facing.Tolerance), 0.f, PI));
					const FVector ToTarget = (ToWorldTransform.GetLocation() - QueryTransform.GetLocation()).GetSafeNormal2D();
					if (FVector::DotProduct(QueryTransform.GetRotation().GetForwardVector(), ToTarget) < FacingCos)
					{
						continue;
					}
				}
			}

			// Return the first item that passes all tests
			DataIndex = Idx;
			break;
		}
	}
	else // Simple Query
	{
		float BestDistanceSq = MAX_FLT;
		for (int32 Idx = 0; Idx < InteractorTrack.AnimDataContainer.Num(); Idx++)
		{
			const FContextualAnimData& Data = InteractorTrack.AnimDataContainer[Idx];

			//@TODO: Convert querier location to local space instead
			const FTransform EntryTransform = Data.GetAlignmentTransformAtEntryTime() * ToWorldTransform;
			const float DistSq = FVector::DistSquared2D(EntryTransform.GetLocation(), QueryTransform.GetLocation());
			if (DistSq < BestDistanceSq)
			{
				BestDistanceSq = DistSq;
				DataIndex = Idx;
			}
		}
	}

	if (DataIndex != INDEX_NONE)
	{
		const FContextualAnimData& ResultData = InteractorTrack.AnimDataContainer[DataIndex];

		OutResult.DataIndex = DataIndex;
		OutResult.Animation = ResultData.Animation;
		OutResult.EntryTransform = ResultData.GetAlignmentTransformAtEntryTime() * ToWorldTransform;
		OutResult.SyncTransform = ResultData.GetAlignmentTransformAtSyncTime() * ToWorldTransform;

		if (QueryParams.bFindAnimStartTime)
		{
			const FVector LocalLocation = (QueryTransform.GetRelativeTransform(ToWorldTransform)).GetLocation();
			OutResult.AnimStartTime = ResultData.FindBestAnimStartTime(LocalLocation);
		}

		return true;
	}

	return false;
}