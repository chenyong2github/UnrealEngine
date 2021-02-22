// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimSceneAsset.h"
#include "AnimationRuntime.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AnimTypes.h"
#include "ContextualAnimUtilities.h"

UContextualAnimSceneAsset::UContextualAnimSceneAsset(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UContextualAnimSceneAsset::PreSave(const class ITargetPlatform* TargetPlatform)
{
	Super::PreSave(TargetPlatform);
	
	const FTransform MeshToComponentInverse = MeshToComponent.Inverse();
	const float SampleInterval = 1.f / SampleRate;

	TArray<FBoneIndexType> RequiredBoneIndexArray = { 0 };

	for (auto& Entry : DataContainer)
	{
		// Generate alignment tracks relative to scene pivot
		GenerateAlignmentTracksRelativeToScenePivot(Entry.Value.AnimData);

		// Generate alignment tracks relative to other actors
		const FName& Role = Entry.Key;
		FContextualAnimData& AnimData = Entry.Value.AnimData;

		const UAnimMontage* Animation = AnimData.Animation;

		UE_LOG(LogContextualAnim, Log, TEXT("%s Generating Alignment Track Relative To Others for '%s' (Anim: %s)"), *GetNameSafe(this), *Role.ToString(), *GetNameSafe(Animation));

		if (Animation)
		{
			FBoneContainer BoneContainer = FBoneContainer(RequiredBoneIndexArray, FCurveEvaluationOption(false), *Animation->GetSkeleton());

			for (const auto& OtherEntry : DataContainer)
			{
				const FName& OtherRole = OtherEntry.Key;
				const FContextualAnimData& OtherAnimData = OtherEntry.Value.AnimData;

				if (Role != OtherRole)
				{
					float Time = 0.f;
					float EndTime = Animation->GetPlayLength();
					int32 SampleIndex = 0;

					AnimData.AlignmentData.Tracks.TrackNames.Add(OtherRole);
					FRawAnimSequenceTrack& NewTrack = AnimData.AlignmentData.Tracks.AnimationTracks.AddZeroed_GetRef();

					const UAnimMontage* OtherAnimation = OtherAnimData.Animation;
					if (OtherAnimation)
					{
						FBoneContainer OtherBoneContainer = FBoneContainer(RequiredBoneIndexArray, FCurveEvaluationOption(false), *OtherAnimation->GetSkeleton());

						while (Time < EndTime)
						{
							Time = FMath::Clamp(SampleIndex * SampleInterval, 0.f, EndTime);
							SampleIndex++;

							FCSPose<FCompactPose> ComponentSpacePose;
							UContextualAnimUtilities::ExtractComponentSpacePose(Animation, BoneContainer, Time, false, ComponentSpacePose);

							FCSPose<FCompactPose> OtherComponentSpacePose;
							UContextualAnimUtilities::ExtractComponentSpacePose(OtherAnimation, OtherBoneContainer, Time, false, OtherComponentSpacePose);

							const FTransform RootTransform = MeshToComponentInverse * (ComponentSpacePose.GetComponentSpaceTransform(FCompactPoseBoneIndex(0)) * AnimData.MeshToScene);
							const FTransform OtherRootTransform = MeshToComponentInverse * (OtherComponentSpacePose.GetComponentSpaceTransform(FCompactPoseBoneIndex(0)) * OtherAnimData.MeshToScene);
							const FTransform RootRelativeToRef = RootTransform.GetRelativeTransform(OtherRootTransform);

							NewTrack.PosKeys.Add(RootRelativeToRef.GetLocation());
							NewTrack.RotKeys.Add(RootRelativeToRef.GetRotation());
							NewTrack.ScaleKeys.Add(RootRelativeToRef.GetScale3D());
						};
					}

					UE_LOG(LogContextualAnim, Log, TEXT("\t Relative To: '%s' (Anim: %s) NumKeys: %d"), *OtherRole.ToString(), *GetNameSafe(OtherAnimation), NewTrack.PosKeys.Num());
				}
			}
		}
	}
}

const FContextualAnimTrack* UContextualAnimSceneAsset::FindTrack(const FName& Role) const
{
	return DataContainer.Find(Role);
}

UClass* UContextualAnimSceneAsset::GetPreviewActorClassForRole(const FName& Role) const
{
	if (const FContextualAnimTrack* Result = FindTrack(Role))
	{
		return Result->Settings.PreviewActorClass;
	}

	return nullptr;
}

EContextualAnimJoinRule UContextualAnimSceneAsset::GetJoinRuleForRole(const FName& Role) const
{
	const FContextualAnimTrack* Result = FindTrack(Role);
	return Result ? Result->Settings.JoinRule : EContextualAnimJoinRule::Default;
}
