// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimSceneAssetBase.h"
#include "AnimationRuntime.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AnimTypes.h"
#include "Containers/ArrayView.h"
#include "ContextualAnimScenePivotProvider.h"
#include "ContextualAnimUtilities.h"
#include "ContextualAnimMetadata.h"

UContextualAnimSceneAssetBase::UContextualAnimSceneAssetBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SampleRate = 15;
	MeshToComponent = FTransform(FRotator(0.f, -90.f, 0.f));
}

FTransform UContextualAnimSceneAssetBase::ExtractTransformFromAnimData(const FContextualAnimData& AnimData, float Time) const
{
	const FTransform MeshToComponentInverse = MeshToComponent.Inverse();

	if (const UAnimMontage* Animation = AnimData.Animation)
	{
		FCSPose<FCompactPose> ComponentSpacePose;
		UContextualAnimUtilities::ExtractComponentSpacePose(Animation, FBoneContainer({ 0 }, FCurveEvaluationOption(false), *Animation->GetSkeleton()), Time, false, ComponentSpacePose);
		return MeshToComponentInverse * (ComponentSpacePose.GetComponentSpaceTransform(FCompactPoseBoneIndex(0)) * AnimData.MeshToScene);
	}
	else
	{
		return MeshToComponentInverse * AnimData.MeshToScene;
	}
}

void UContextualAnimSceneAssetBase::GenerateAlignmentTracksRelativeToScenePivot(FContextualAnimData& AnimData)
{
	const FTransform MeshToComponentInverse = MeshToComponent.Inverse();
	const float SampleInterval = 1.f / SampleRate;

	const int32 TotalTracks = AlignmentSections.Num();
	AnimData.AlignmentData.Tracks.AnimationTracks.Reset(TotalTracks);
	AnimData.AlignmentData.Tracks.TrackNames.Reset(TotalTracks);
	AnimData.AlignmentData.SampleInterval = SampleInterval;

	const UAnimMontage* Animation = AnimData.Animation;
	if (Animation)
	{
		TArray<FBoneIndexType> RequiredBoneIndexArray;
		RequiredBoneIndexArray.Add(0);
		FBoneContainer BoneContainer = FBoneContainer(RequiredBoneIndexArray, FCurveEvaluationOption(false), *Animation->GetSkeleton());

		for (int32 Idx = 0; Idx < AlignmentSections.Num(); Idx++)
		{
			float Time = 0.f;
			float EndTime = Animation->GetPlayLength();
			int32 SampleIndex = 0;

			AnimData.AlignmentData.Tracks.TrackNames.Add(AlignmentSections[Idx].SectionName);
			FRawAnimSequenceTrack& SceneTrack = AnimData.AlignmentData.Tracks.AnimationTracks.AddZeroed_GetRef();
			while (Time < EndTime)
			{
				Time = FMath::Clamp(SampleIndex * SampleInterval, 0.f, EndTime);
				SampleIndex++;

				FCSPose<FCompactPose> ComponentSpacePose;
				UContextualAnimUtilities::ExtractComponentSpacePose(Animation, BoneContainer, Time, false, ComponentSpacePose);

				const FTransform RootTransform = MeshToComponentInverse * (ComponentSpacePose.GetComponentSpaceTransform(FCompactPoseBoneIndex(0)) * AnimData.MeshToScene);
				const FTransform ScenePivotTransform = AlignmentSections[Idx].ScenePivot;
				const FTransform RootRelativeToScenePivot = RootTransform.GetRelativeTransform(ScenePivotTransform);

				SceneTrack.PosKeys.Add(RootRelativeToScenePivot.GetLocation());
				SceneTrack.RotKeys.Add(RootRelativeToScenePivot.GetRotation());
				SceneTrack.ScaleKeys.Add(RootRelativeToScenePivot.GetScale3D());
			}
		}
	}
	else
	{
		for (int32 Idx = 0; Idx < AlignmentSections.Num(); Idx++)
		{
			AnimData.AlignmentData.Tracks.TrackNames.Add(AlignmentSections[Idx].SectionName);
			FRawAnimSequenceTrack& SceneTrack = AnimData.AlignmentData.Tracks.AnimationTracks.AddZeroed_GetRef();

			const FTransform RootTransform = MeshToComponentInverse * AnimData.MeshToScene;
			const FTransform ScenePivotTransform = AlignmentSections[Idx].ScenePivot;
			const FTransform RootRelativeToScenePivot = RootTransform.GetRelativeTransform(ScenePivotTransform);

			SceneTrack.PosKeys.Add(RootRelativeToScenePivot.GetLocation());
			SceneTrack.RotKeys.Add(RootRelativeToScenePivot.GetRotation());
			SceneTrack.ScaleKeys.Add(RootRelativeToScenePivot.GetScale3D());
		}
	}
}

void UContextualAnimSceneAssetBase::PreSave(const class ITargetPlatform* TargetPlatform)
{
	Super::PreSave(TargetPlatform);

	// Generate scene pivot for each alignment section
	for (int32 Idx = 0; Idx < AlignmentSections.Num(); Idx++)
	{
		if (AlignmentSections[Idx].ScenePivotProvider)
		{
			AlignmentSections[Idx].ScenePivot = AlignmentSections[Idx].ScenePivotProvider->CalculateScenePivot_Source();
		}
		else
		{
			AlignmentSections[Idx].ScenePivot = MeshToComponent.Inverse();
		}
	}
}
