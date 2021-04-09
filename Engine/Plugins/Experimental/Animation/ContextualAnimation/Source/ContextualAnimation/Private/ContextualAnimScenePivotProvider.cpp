// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimScenePivotProvider.h"
#include "ContextualAnimSceneAsset.h"
#include "ContextualAnimUtilities.h"

// UContextualAnimScenePivotProvider
//==================================================

UContextualAnimScenePivotProvider::UContextualAnimScenePivotProvider(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

const UContextualAnimSceneAsset* UContextualAnimScenePivotProvider::GetSceneAsset() const
{
	return CastChecked<UContextualAnimSceneAsset>(GetOuter());
}

// UContextualAnimScenePivotProvider_Default
//==================================================

UContextualAnimScenePivotProvider_Default::UContextualAnimScenePivotProvider_Default(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FTransform UContextualAnimScenePivotProvider_Default::CalculateScenePivot_Source(int32 AnimDataIndex) const
{
	FTransform ScenePivot = FTransform::Identity;

	if(const UContextualAnimSceneAsset* SceneAsset = Cast<UContextualAnimSceneAsset>(GetSceneAsset()))
	{
		if(const FContextualAnimCompositeTrack* PrimaryTrack = SceneAsset->DataContainer.Find(PrimaryRole))
		{
			if(const FContextualAnimCompositeTrack* SecondaryTrack = SceneAsset->DataContainer.Find(SecondaryRole))
			{
				const FTransform PrimaryTransform = PrimaryTrack->GetRootTransformForAnimDataAtIndex(AnimDataIndex);
				const FTransform SecondaryTransform = SecondaryTrack->GetRootTransformForAnimDataAtIndex(AnimDataIndex);
				
				ScenePivot.SetLocation(FMath::Lerp<FVector>(PrimaryTransform.GetLocation(), SecondaryTransform.GetLocation(), Weight));
				ScenePivot.SetRotation((SecondaryTransform.GetLocation() - PrimaryTransform.GetLocation()).GetSafeNormal2D().ToOrientationQuat());
			}
		}
	}

	return ScenePivot;
}

FTransform UContextualAnimScenePivotProvider_Default::CalculateScenePivot_Runtime(const TMap<FName, FContextualAnimSceneActorData>& SceneActorMap) const
{
	FTransform ScenePivot = FTransform::Identity;

	if (const AActor* PrimaryActor = SceneActorMap.Find(PrimaryRole)->GetActor())
	{
		if (const AActor* SecondaryActor = SceneActorMap.Find(SecondaryRole)->GetActor())
		{
			ScenePivot.SetLocation(FMath::Lerp<FVector>(PrimaryActor->GetActorLocation(), SecondaryActor->GetActorLocation(), Weight));
			ScenePivot.SetRotation((SecondaryActor->GetActorLocation() - PrimaryActor->GetActorLocation()).GetSafeNormal2D().ToOrientationQuat());
		}
	}

	return ScenePivot;
}

// UContextualAnimScenePivotProvider_RelativeTo
//==================================================

UContextualAnimScenePivotProvider_RelativeTo::UContextualAnimScenePivotProvider_RelativeTo(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FTransform UContextualAnimScenePivotProvider_RelativeTo::CalculateScenePivot_Source(int32 AnimDataIndex) const
{
	FTransform ScenePivot = FTransform::Identity;

	if (const UContextualAnimSceneAsset* SceneAsset = Cast<UContextualAnimSceneAsset>(GetSceneAsset()))
	{
		if(const FContextualAnimCompositeTrack* Track = SceneAsset->DataContainer.Find(RelativeToRole))
		{
			ScenePivot = Track->GetRootTransformForAnimDataAtIndex(AnimDataIndex);
		}
	}

	return ScenePivot;
}

FTransform UContextualAnimScenePivotProvider_RelativeTo::CalculateScenePivot_Runtime(const TMap<FName, FContextualAnimSceneActorData>& SceneActorMap) const
{
	FTransform ScenePivot = FTransform::Identity;

	if (const FContextualAnimSceneActorData* Data = SceneActorMap.Find(RelativeToRole))
	{
		if (const AActor* Actor = Data->GetActor())
		{
			ScenePivot = Actor->GetActorTransform();
		}
	}

	return ScenePivot;
}