// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimPose.h"
#include "PreviewScene.h"
#include "Animation/AnimNode_LinkedInputPose.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimSequence.h"

DEFINE_LOG_CATEGORY_STATIC(LogAnimationPoseScripting, Verbose, All);

void FAnimPose::Init(const FBoneContainer& InBoneContainer)
{
	Reset();
	BoneContainer = InBoneContainer;

	const FReferenceSkeleton& RefSkeleton = BoneContainer.GetSkeletonAsset()->GetReferenceSkeleton();
	
	for (const FBoneIndexType BoneIndex : BoneContainer.GetBoneIndicesArray())
	{			
		const FCompactPoseBoneIndex CompactIndex(BoneIndex);
		const FCompactPoseBoneIndex CompactParentIndex = BoneContainer.GetParentBoneIndex(CompactIndex);

		const int32 SkeletonBoneIndex = BoneContainer.GetSkeletonIndex(CompactIndex);
		if (SkeletonBoneIndex != INDEX_NONE)
		{
			const int32 ParentBoneIndex = CompactParentIndex.GetInt() != INDEX_NONE ? BoneContainer.GetSkeletonIndex(CompactParentIndex) : INDEX_NONE;

			BoneIndices.Add(SkeletonBoneIndex);
			ParentBoneIndices.Add(ParentBoneIndex);

			BoneNames.Add(RefSkeleton.GetBoneName(SkeletonBoneIndex));

			RefLocalSpacePoses.Add(BoneContainer.GetRefPoseTransform(FCompactPoseBoneIndex(BoneIndex)));
		}
	}

	TArray<bool> Processed;
	Processed.SetNumZeroed(BoneNames.Num());
	RefWorldSpacePoses.SetNum(BoneNames.Num());
	for (int32 EntryIndex = 0; EntryIndex < BoneNames.Num(); ++EntryIndex)
	{
		const int32 ParentIndex = ParentBoneIndices[EntryIndex];
		const int32 TransformIndex = BoneIndices.IndexOfByKey(ParentIndex);

		if (TransformIndex != INDEX_NONE)
		{
			ensure(Processed[TransformIndex]);
			RefWorldSpacePoses[EntryIndex] = RefLocalSpacePoses[EntryIndex] * RefWorldSpacePoses[TransformIndex];
		}
		else
		{
			RefWorldSpacePoses[EntryIndex] = RefLocalSpacePoses[EntryIndex];
		}

		Processed[EntryIndex] = true;
	}
}

void FAnimPose::GetPose(FCompactPose& InOutCompactPose) const
{
	if (IsValid())
	{
		for (int32 Index = 0; Index < BoneNames.Num(); ++Index)
		{
			const FName& BoneName = BoneNames[Index];
			const FCompactPoseBoneIndex PoseBoneIndex = FCompactPoseBoneIndex(InOutCompactPose.GetBoneContainer().GetPoseBoneIndexForBoneName(BoneName));
			if (PoseBoneIndex != INDEX_NONE)
			{
				InOutCompactPose[PoseBoneIndex] = LocalSpacePoses[Index];
			}
		}
	}
}

void FAnimPose::SetPose(USkeletalMeshComponent* Component)
{
	if (IsInitialized())
	{		
		const FBoneContainer& ContextBoneContainer = Component->GetAnimInstance()->GetRequiredBones();

		LocalSpacePoses.SetNum(RefLocalSpacePoses.Num());

		TArray<FTransform> BoneSpaceTransforms = Component->GetBoneSpaceTransforms();
		for (const FBoneIndexType BoneIndex : ContextBoneContainer.GetBoneIndicesArray())
		{
			const int32 SkeletonBoneIndex = ContextBoneContainer.GetSkeletonIndex(FCompactPoseBoneIndex(BoneIndex));
			LocalSpacePoses[BoneIndices.IndexOfByKey(SkeletonBoneIndex)] = BoneSpaceTransforms[BoneIndex];
		}

		ensure(LocalSpacePoses.Num() == RefLocalSpacePoses.Num());	
		GenerateWorldSpaceTransforms();
	}
}

void FAnimPose::GenerateWorldSpaceTransforms()
{
	if (IsPopulated())
	{
		TArray<bool> Processed;
		Processed.SetNumZeroed(BoneNames.Num());
		WorldSpacePoses.SetNum(BoneNames.Num());
		for (int32 EntryIndex = 0; EntryIndex < BoneNames.Num(); ++EntryIndex)
		{
			const int32 ParentIndex = ParentBoneIndices[EntryIndex];
			const int32 TransformIndex = BoneIndices.IndexOfByKey(ParentIndex);
			if (TransformIndex != INDEX_NONE)
			{
				ensure(Processed[TransformIndex]);
				WorldSpacePoses[EntryIndex] = LocalSpacePoses[EntryIndex] * WorldSpacePoses[TransformIndex];
			}
			else
			{
				WorldSpacePoses[EntryIndex] = LocalSpacePoses[EntryIndex];
			}

			Processed[EntryIndex] = true;
		}
	}
	else
	{
		UE_LOG(LogAnimationPoseScripting, Warning, TEXT("Anim Pose was not previously populated"));
	}
}

void FAnimPose::SetPose(const FCompactPose& CompactPose)
{
	if (IsInitialized())
	{
		const FBoneContainer& ContextBoneContainer = CompactPose.GetBoneContainer();
			
		LocalSpacePoses.SetNum(RefLocalSpacePoses.Num());
		for (const FCompactPoseBoneIndex BoneIndex : CompactPose.ForEachBoneIndex())
		{
			const int32 SkeletonBoneIndex = ContextBoneContainer.GetSkeletonIndex(BoneIndex);
			LocalSpacePoses[BoneIndices.IndexOfByKey(SkeletonBoneIndex)] = CompactPose[BoneIndex];
		}

		ensure(LocalSpacePoses.Num() == RefLocalSpacePoses.Num());
		GenerateWorldSpaceTransforms();
	}
	else
	{
		UE_LOG(LogAnimationPoseScripting, Warning, TEXT("Anim Pose was not previously initialized"));
	}
}

void FAnimPose::SetToRefPose()
{
	if (IsInitialized())
	{
		LocalSpacePoses = RefLocalSpacePoses;
		WorldSpacePoses = RefWorldSpacePoses;
	}
	else
	{
		UE_LOG(LogAnimationPoseScripting, Warning, TEXT("Anim Pose was not previously initialized"));
	}
}

bool FAnimPose::IsValid() const
{
	const int32 ExpectedNumBones = BoneNames.Num();
	bool bIsValid = ExpectedNumBones != 0;
	
	bIsValid &= BoneIndices.Num() == ExpectedNumBones;
	bIsValid &= ParentBoneIndices.Num() == ExpectedNumBones;
	bIsValid &= LocalSpacePoses.Num() == ExpectedNumBones;
	bIsValid &= WorldSpacePoses.Num() == ExpectedNumBones;
	bIsValid &= RefLocalSpacePoses.Num() == ExpectedNumBones;
	bIsValid &= RefWorldSpacePoses.Num() == ExpectedNumBones;
	bIsValid &= BoneContainer.GetNumBones() == ExpectedNumBones;
	
	return bIsValid;
}

void FAnimPose::Reset()
{
	BoneNames.Empty();
	BoneIndices.Empty();
	ParentBoneIndices.Empty();
	LocalSpacePoses.Empty();
	WorldSpacePoses.Empty();
	RefLocalSpacePoses.Empty();
	RefWorldSpacePoses.Empty();
}

bool UAnimPoseExtensions::IsValid(const FAnimPose& Pose)
{
	return Pose.IsValid();
}

void UAnimPoseExtensions::GetBoneNames(const FAnimPose& Pose, TArray<FName>& Bones)
{
	Bones.Append(Pose.BoneNames);
}

const FTransform& UAnimPoseExtensions::GetBonePose(const FAnimPose& Pose, FName BoneName, EAnimPoseSpaces Space /*= EAnimPoseSpaces::Local*/)
{
	if (Pose.IsValid())
	{
		const int32 BoneIndex = Pose.BoneNames.IndexOfByKey(BoneName);
		if (BoneIndex != INDEX_NONE)
		{		
			return Space == EAnimPoseSpaces::Local ? Pose.LocalSpacePoses[BoneIndex] : Pose.WorldSpacePoses[BoneIndex];		
		}
		else
		{
			UE_LOG(LogAnimationPoseScripting, Warning, TEXT("No bone with name %s was found"), *BoneName.ToString());
		}
	}
	else
	{		
		UE_LOG(LogAnimationPoseScripting, Error, TEXT("Provided Pose is not valid"));	
	}
	return FTransform::Identity;
}

void UAnimPoseExtensions::SetBonePose(FAnimPose& Pose, FTransform Transform, FName BoneName, EAnimPoseSpaces Space /*= EAnimPoseSpaces::Local*/)
{
	if (Pose.IsValid())
	{
		const int32 BoneIndex = Pose.BoneNames.IndexOfByKey(BoneName);
		if (BoneIndex != INDEX_NONE)
		{
			if (Space == EAnimPoseSpaces::Local)
			{
				Pose.LocalSpacePoses[BoneIndex] = Transform;
			}
			else if (Space == EAnimPoseSpaces::World)
			{
				const int32 ParentIndex = Pose.ParentBoneIndices[BoneIndex];
				const FTransform ParentTransformWS = ParentIndex != INDEX_NONE ? Pose.WorldSpacePoses[ParentIndex] : FTransform::Identity;
				Pose.LocalSpacePoses[BoneIndex] = Transform.GetRelativeTransform(ParentTransformWS);
			}
			
			Pose.GenerateWorldSpaceTransforms();
		}
		else
		{
			UE_LOG(LogAnimationPoseScripting, Warning, TEXT("No bone with name %s was found"), *BoneName.ToString());
		}
	}
	else
	{		
		UE_LOG(LogAnimationPoseScripting, Error, TEXT("Provided Pose is not valid"));	
	}
}
	
const FTransform& UAnimPoseExtensions::GetRefBonePose(const FAnimPose& Pose, FName BoneName, EAnimPoseSpaces Space /*= EAnimPoseSpaces::Local*/)
{
	if (Pose.IsValid())
	{
		const int32 BoneIndex = Pose.BoneNames.IndexOfByKey(BoneName);
		if (BoneIndex != INDEX_NONE)
		{	
			return Space == EAnimPoseSpaces::Local ? Pose.RefLocalSpacePoses[BoneIndex] : Pose.RefWorldSpacePoses[BoneIndex];
		}
		else
		{
			UE_LOG(LogAnimationPoseScripting, Warning, TEXT("No bone with name %s was found"), *BoneName.ToString());
		}
	}
	else
	{		
		UE_LOG(LogAnimationPoseScripting, Error, TEXT("Provided Pose is not valid"));	
	}

	return FTransform::Identity;	
}

FTransform UAnimPoseExtensions::GetRelativeTransform(const FAnimPose& Pose, FName FromBoneName, FName ToBoneName, EAnimPoseSpaces Space /*= EAnimPoseSpaces::Local*/)
{
	if (Pose.IsValid())
	{
		const int32 FromBoneIndex = Pose.BoneNames.IndexOfByKey(FromBoneName);
		const int32 ToBoneIndex = Pose.BoneNames.IndexOfByKey(ToBoneName);
		if (FromBoneIndex != INDEX_NONE && ToBoneIndex != INDEX_NONE)
		{	
			const FTransform& From = Space == EAnimPoseSpaces::Local ? Pose.LocalSpacePoses[FromBoneIndex] : Pose.WorldSpacePoses[FromBoneIndex];
			const FTransform& To = Space == EAnimPoseSpaces::Local ? Pose.LocalSpacePoses[ToBoneIndex] : Pose.WorldSpacePoses[ToBoneIndex];

			const FTransform Relative = To.GetRelativeTransform(From);
				
			return Relative;
		}
		else
		{
			UE_LOG(LogAnimationPoseScripting, Warning, TEXT("No bone with name %s or %s was found"), *FromBoneName.ToString(), *ToBoneName.ToString());
		}
	}
	else
	{		
		UE_LOG(LogAnimationPoseScripting, Error, TEXT("Provided Pose is not valid"));	
	}

	return FTransform::Identity;
}

FTransform UAnimPoseExtensions::GetRelativeToRefPoseTransform(const FAnimPose& Pose, FName BoneName, EAnimPoseSpaces Space /*= EAnimPoseSpaces::Local*/)
{
	if (Pose.IsValid())
	{
		const int32 BoneIndex = Pose.BoneNames.IndexOfByKey(BoneName);
		if (BoneIndex != INDEX_NONE)
		{	
			const FTransform& From = Space == EAnimPoseSpaces::Local ? Pose.RefLocalSpacePoses[BoneIndex] : Pose.RefWorldSpacePoses[BoneIndex];
			const FTransform& To = Space == EAnimPoseSpaces::Local ? Pose.LocalSpacePoses[BoneIndex] : Pose.WorldSpacePoses[BoneIndex];

			return To.GetRelativeTransform(From);
		}
		else
		{
			UE_LOG(LogAnimationPoseScripting, Warning, TEXT("No bone with name %s was found"), *BoneName.ToString());
		}
	}
	else
	{		
		UE_LOG(LogAnimationPoseScripting, Error, TEXT("Provided Pose is not valid"));	
	}

	return FTransform::Identity;
}

FTransform UAnimPoseExtensions::GetRefPoseRelativeTransform(const FAnimPose& Pose, FName FromBoneName, FName ToBoneName, EAnimPoseSpaces Space /*= EAnimPoseSpaces::Local*/)
{
	if (Pose.IsValid())
	{
		const int32 FromBoneIndex = Pose.BoneNames.IndexOfByKey(FromBoneName);
		const int32 ToBoneIndex = Pose.BoneNames.IndexOfByKey(ToBoneName);
		if (FromBoneIndex != INDEX_NONE && ToBoneIndex != INDEX_NONE)
		{	
			const FTransform& From = Space == EAnimPoseSpaces::Local ? Pose.RefLocalSpacePoses[FromBoneIndex] : Pose.RefWorldSpacePoses[FromBoneIndex];
			const FTransform& To = Space == EAnimPoseSpaces::Local ? Pose.RefLocalSpacePoses[ToBoneIndex] : Pose.RefWorldSpacePoses[ToBoneIndex];

			const FTransform Relative = From.GetRelativeTransform(To);
				
			return Relative;
		}
		else
		{
			UE_LOG(LogAnimationPoseScripting, Warning, TEXT("No bone with name %s or %s was found"), *FromBoneName.ToString(), *ToBoneName.ToString());
		}
	}
	else
	{		
		UE_LOG(LogAnimationPoseScripting, Error, TEXT("Provided Pose is not valid"));	
	}

	return FTransform::Identity;
}

void UAnimPoseExtensions::EvaluateAnimationBlueprintWithInputPose(const FAnimPose& Pose, USkeletalMesh* TargetSkeletalMesh, UAnimBlueprint* AnimationBlueprint, FAnimPose& OutPose)
{
	if (Pose.IsValid())
	{
		if (TargetSkeletalMesh)
		{
			if (AnimationBlueprint)
			{
				UAnimBlueprintGeneratedClass* AnimGeneratedClass = AnimationBlueprint->GetAnimBlueprintGeneratedClass();
				if (AnimGeneratedClass)
				{
					if (AnimGeneratedClass->TargetSkeleton == TargetSkeletalMesh->GetSkeleton())
					{
						FMemMark Mark(FMemStack::Get());
						
						FPreviewScene PreviewScene;
			
						USkeletalMeshComponent* Component = NewObject<USkeletalMeshComponent>();
						Component->SetSkeletalMesh(TargetSkeletalMesh);
						Component->SetAnimInstanceClass(AnimationBlueprint->GetAnimBlueprintGeneratedClass());

						PreviewScene.AddComponent(Component, FTransform::Identity);
			
						if (UAnimInstance* AnimInstance = Component->GetAnimInstance())
						{
							if (FAnimNode_LinkedInputPose* InputNode = AnimInstance->GetLinkedInputPoseNode())
							{
								const FBoneContainer& BoneContainer = AnimInstance->GetRequiredBones();
								InputNode->CachedInputPose.SetBoneContainer(&BoneContainer);
								InputNode->CachedInputCurve.InitFrom(BoneContainer);
								InputNode->CachedInputPose.ResetToRefPose();

								// Copy bone transform from input pose using skeleton index mapping
								for (FCompactPoseBoneIndex CompactIndex : InputNode->CachedInputPose.ForEachBoneIndex())
								{
									const int32 SkeletonIndex = BoneContainer.GetSkeletonIndex(CompactIndex);
									if (SkeletonIndex != INDEX_NONE)
									{
										const int32 Index = Pose.BoneIndices.IndexOfByKey(SkeletonIndex);
										if (Index != INDEX_NONE)
										{
											InputNode->CachedInputPose[CompactIndex] = Pose.LocalSpacePoses[Index];
										}
									}
								}
					
								OutPose.Init(AnimInstance->GetRequiredBones());

								Component->InitAnim(true);
								Component->RefreshBoneTransforms();
								const TArray<FTransform>& LocalSpaceTransforms = Component->GetBoneSpaceTransforms();

								OutPose.SetPose(Component);	
							}	
						}
						else
						{
							UE_LOG(LogAnimationPoseScripting, Warning, TEXT("Failed to retrieve Input Pose Node from Animation Graph %s"), *AnimationBlueprint->GetName());	
						}
					}
					else
					{
						UE_LOG(LogAnimationPoseScripting, Error, TEXT("Animation Blueprint target Skeleton %s does not match Target Skeletal Mesh its Skeleton %s"), *AnimGeneratedClass->TargetSkeleton->GetName(), *TargetSkeletalMesh->GetSkeleton()->GetName());	
					}
				
				}
				else
				{
					UE_LOG(LogAnimationPoseScripting, Warning, TEXT("Failed to retrieve Animation Blueprint generated class"));	
				}
			}
			else
			{
				UE_LOG(LogAnimationPoseScripting, Warning, TEXT("Invalid Animation Blueprint"));			
			}	
		}
		else
		{		
			UE_LOG(LogAnimationPoseScripting, Warning, TEXT("Invalid Target Skeletal Mesh"));
		}
	}
	else
	{		
		UE_LOG(LogAnimationPoseScripting, Error, TEXT("Provided Pose is not valid"));	
	}
}

void UAnimPoseExtensions::GetReferencePose(USkeleton* Skeleton, FAnimPose& OutPose)
{
	if (Skeleton)
	{
		const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
				       
		TArray<FBoneIndexType> RequiredBoneIndexArray;
		RequiredBoneIndexArray.AddUninitialized(RefSkeleton.GetNum());
		for (int32 BoneIndex = 0; BoneIndex < RequiredBoneIndexArray.Num(); ++BoneIndex)
		{
			RequiredBoneIndexArray[BoneIndex] = BoneIndex;
		}

		FBoneContainer RequiredBones;
		RequiredBones.InitializeTo(RequiredBoneIndexArray, FCurveEvaluationOption(false), *Skeleton);

		OutPose.Init(RequiredBones);
		OutPose.SetToRefPose();
	}
	else
	{		
		UE_LOG(LogAnimationPoseScripting, Error, TEXT("Invalid Skeleton provided"));	
	}
}

void UAnimPoseExtensions::GetAnimPoseAtFrame(const UAnimSequenceBase* AnimationSequenceBase, int32 FrameIndex, FAnimPoseEvaluationOptions EvaluationOptions, FAnimPose& Pose)
{
	float Time = 0.f;
	UAnimationBlueprintLibrary::GetTimeAtFrame(AnimationSequenceBase, FrameIndex, Time);
	GetAnimPoseAtTime(AnimationSequenceBase, Time, EvaluationOptions, Pose);
}

void UAnimPoseExtensions::GetAnimPoseAtTime(const UAnimSequenceBase* AnimationSequenceBase, float Time, FAnimPoseEvaluationOptions EvaluationOptions, FAnimPose& Pose)
{
	if (AnimationSequenceBase && AnimationSequenceBase->GetSkeleton())
	{
		FMemMark Mark(FMemStack::Get());

		bool bValidTime = false;
		UAnimationBlueprintLibrary::IsValidTime(AnimationSequenceBase, Time, bValidTime);
		if (bValidTime)
		{	
			// asset to use for retarget proportions (can be either USkeletalMesh or USkeleton)
			UObject* AssetToUse;
			int32 NumRequiredBones;
			if (EvaluationOptions.OptionalSkeletalMesh)
			{
				AssetToUse = CastChecked<UObject>(EvaluationOptions.OptionalSkeletalMesh);
				NumRequiredBones = EvaluationOptions.OptionalSkeletalMesh->GetRefSkeleton().GetNum();	
			}
			else
			{
				AssetToUse = CastChecked<UObject>(AnimationSequenceBase->GetSkeleton());
				NumRequiredBones = AnimationSequenceBase->GetSkeleton()->GetReferenceSkeleton().GetNum();
			}

			TArray<FBoneIndexType> RequiredBoneIndexArray;
			RequiredBoneIndexArray.AddUninitialized(NumRequiredBones);
			for (int32 BoneIndex = 0; BoneIndex < RequiredBoneIndexArray.Num(); ++BoneIndex)
			{
				RequiredBoneIndexArray[BoneIndex] = BoneIndex;
			}

			FBoneContainer RequiredBones;
			RequiredBones.InitializeTo(RequiredBoneIndexArray, FCurveEvaluationOption(false), *AssetToUse);
			
			RequiredBones.SetUseRAWData(EvaluationOptions.EvaluationType == EAnimDataEvalType::Raw);
			RequiredBones.SetUseSourceData(EvaluationOptions.EvaluationType == EAnimDataEvalType::Source);
			
			RequiredBones.SetDisableRetargeting(!EvaluationOptions.bShouldRetarget);
			
			FCompactPose CompactPose;
			CompactPose.SetBoneContainer(&RequiredBones);

			Pose.Init(CompactPose.GetBoneContainer());

			FBlendedCurve Curve;
			Curve.InitFrom(RequiredBones);
			UE::Anim::FStackAttributeContainer Attributes;

			FAnimationPoseData PoseData(CompactPose, Curve, Attributes);
			FAnimExtractContext Context(Time, EvaluationOptions.bExtractRootMotion);

			AnimationSequenceBase->GetAnimationPose(PoseData, Context);

			if (AnimationSequenceBase->IsValidAdditive())
			{
				const UAnimSequence* AnimSequence = Cast<const UAnimSequence>(AnimationSequenceBase);
				FCompactPose BasePose;
				BasePose.SetBoneContainer(&RequiredBones);

				FBlendedCurve BaseCurve;
				BaseCurve.InitFrom(RequiredBones);
				UE::Anim::FStackAttributeContainer BaseAttributes;
				
				FAnimationPoseData BasePoseData(BasePose, BaseCurve, BaseAttributes);
				AnimSequence->GetAdditiveBasePose(BasePoseData, Context);

				FAnimationRuntime::AccumulateAdditivePose(BasePoseData, PoseData, 1.f, AnimSequence->GetAdditiveAnimType());
				BasePose.NormalizeRotations();
				
				Pose.SetPose(BasePose);
			}
			else
			{
				Pose.SetPose(CompactPose);
			}
		}
		else
		{
			UE_LOG(LogAnimationPoseScripting, Warning, TEXT("Invalid time value %f for Animation Sequence %s supplied for GetBonePosesForTime"), Time, *AnimationSequenceBase->GetName());
		}
	}
	else
	{
		UE_LOG(LogAnimationPoseScripting, Warning, TEXT("Invalid Animation Sequence supplied for GetBonePosesForTime"));
	}
}
