// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimUtilities.h"
#include "AnimationUtils.h"
#include "AnimationRuntime.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AttributesRuntime.h"
#include "Animation/AnimTypes.h"
#include "Animation/AnimInstance.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "ContextualAnimTypes.h"
#include "ContextualAnimSceneAsset.h"
#include "Misc/MemStack.h"
#include "GameFramework/Character.h"
#include "ContextualAnimActorInterface.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "AnimNotifyState_IKWindow.h"
#include "SceneManagement.h"

void UContextualAnimUtilities::ExtractLocalSpacePose(const UAnimSequenceBase* Animation, const FBoneContainer& BoneContainer, float Time, bool bExtractRootMotion, FCompactPose& OutPose)
{
	OutPose.SetBoneContainer(&BoneContainer);

	FBlendedCurve Curve;
	Curve.InitFrom(BoneContainer);

	FAnimExtractContext Context(Time, bExtractRootMotion);

	UE::Anim::FStackAttributeContainer Attributes;
	FAnimationPoseData AnimationPoseData(OutPose, Curve, Attributes);
	if (const UAnimSequence* AnimSequence = Cast<UAnimSequence>(Animation))
	{
		AnimSequence->GetBonePose(AnimationPoseData, Context);
	}
	else if (const UAnimMontage* AnimMontage = Cast<UAnimMontage>(Animation))
	{
		const FAnimTrack& AnimTrack = AnimMontage->SlotAnimTracks[0].AnimTrack;
		AnimTrack.GetAnimationPose(AnimationPoseData, Context);
	}
}

void UContextualAnimUtilities::ExtractComponentSpacePose(const UAnimSequenceBase* Animation, const FBoneContainer& BoneContainer, float Time, bool bExtractRootMotion, FCSPose<FCompactPose>& OutPose)
{
	FCompactPose Pose;
	ExtractLocalSpacePose(Animation, BoneContainer, Time, bExtractRootMotion, Pose);
	OutPose.InitPose(MoveTemp(Pose));
}

FTransform UContextualAnimUtilities::ExtractRootMotionFromAnimation(const UAnimSequenceBase* Animation, float StartTime, float EndTime)
{
	if (const UAnimMontage* Anim = Cast<UAnimMontage>(Animation))
	{
		return Anim->ExtractRootMotionFromTrackRange(StartTime, EndTime);
	}

	if (const UAnimSequence* Anim = Cast<UAnimSequence>(Animation))
	{
		return Anim->ExtractRootMotionFromRange(StartTime, EndTime);
	}

	return FTransform::Identity;
}

FTransform UContextualAnimUtilities::ExtractRootTransformFromAnimation(const UAnimSequenceBase* Animation, float Time)
{
	if (const UAnimMontage* AnimMontage = Cast<UAnimMontage>(Animation))
	{
		if (const FAnimSegment* Segment = AnimMontage->SlotAnimTracks[0].AnimTrack.GetSegmentAtTime(Time))
		{
			if (const UAnimSequence* AnimSequence = Cast<UAnimSequence>(Segment->GetAnimReference()))
			{
				const float AnimSequenceTime = Segment->ConvertTrackPosToAnimPos(Time);
				return AnimSequence->ExtractRootTrackTransform(AnimSequenceTime, nullptr);
			}
		}
	}
	else if (const UAnimSequence* AnimSequence = Cast<UAnimSequence>(Animation))
	{
		return AnimSequence->ExtractRootTrackTransform(Time, nullptr);
	}

	return FTransform::Identity;
}

void UContextualAnimUtilities::DrawDebugPose(const UWorld* World, const UAnimSequenceBase* Animation, float Time, const FTransform& LocalToWorldTransform, const FColor& Color, float LifeTime, float Thickness)
{ 
	if(World)
	{
		FMemMark Mark(FMemStack::Get());

		Time = FMath::Clamp(Time, 0.f, Animation->GetPlayLength());

		const int32 TotalBones = Animation->GetSkeleton()->GetReferenceSkeleton().GetNum();
		TArray<FBoneIndexType> RequiredBoneIndexArray;
		RequiredBoneIndexArray.Reserve(TotalBones);
		for (int32 Idx = 0; Idx < TotalBones; Idx++)
		{
			RequiredBoneIndexArray.Add(Idx);
		}

		FBoneContainer BoneContainer(RequiredBoneIndexArray, FCurveEvaluationOption(false), *Animation->GetSkeleton());
		FCSPose<FCompactPose> ComponentSpacePose;
		UContextualAnimUtilities::ExtractComponentSpacePose(Animation, BoneContainer, Time, true, ComponentSpacePose);

		for (int32 Index = 0; Index < ComponentSpacePose.GetPose().GetNumBones(); ++Index)
		{
			const FCompactPoseBoneIndex CompactPoseBoneIndex = FCompactPoseBoneIndex(Index);
			const FCompactPoseBoneIndex ParentIndex = ComponentSpacePose.GetPose().GetParentBoneIndex(CompactPoseBoneIndex);
			FVector Start, End;

			FLinearColor LineColor = FLinearColor::Red;
			const FTransform Transform = ComponentSpacePose.GetComponentSpaceTransform(CompactPoseBoneIndex) * LocalToWorldTransform;

			if (ParentIndex.GetInt() >= 0)
			{
				Start = (ComponentSpacePose.GetComponentSpaceTransform(ParentIndex) * LocalToWorldTransform).GetLocation();
				End = Transform.GetLocation();
			}
			else
			{
				Start = LocalToWorldTransform.GetLocation();
				End = Transform.GetLocation();
			}

			DrawDebugLine(World, Start, End, Color, false, LifeTime, 0, Thickness);
		}
	}
}

void UContextualAnimUtilities::DrawDebugScene(const UWorld* World, const UContextualAnimSceneAsset* SceneAsset, int32 VariantIdx, float Time, const FTransform& ToWorldTransform, const FColor& Color, float LifeTime, float Thickness)
{
	if (World && SceneAsset)
	{
		SceneAsset->ForEachAnimTrack(VariantIdx, [=](const FContextualAnimTrack& AnimTrack)
		{
			const FTransform Transform = (SceneAsset->GetMeshToComponentForRole(AnimTrack.Role) * AnimTrack.GetAlignmentTransformAtTime(Time)) * ToWorldTransform;
			
			if (const UAnimSequenceBase* Animation = AnimTrack.Animation)
			{
				DrawDebugPose(World, Animation, Time, Transform, Color, LifeTime, Thickness);
			}
			else
			{
				DrawDebugCoordinateSystem(World, Transform.GetLocation(), Transform.Rotator(), 50.f, false, LifeTime, 0, Thickness);
			}

			return UE::ContextualAnim::EForEachResult::Continue;
		});
	}
}

USkeletalMeshComponent* UContextualAnimUtilities::TryGetSkeletalMeshComponent(const AActor* Actor)
{
	USkeletalMeshComponent* SkelMeshComp = nullptr;
	if (Actor)
	{
		if (const ACharacter* Character = Cast<const ACharacter>(Actor))
		{
			SkelMeshComp = Character->GetMesh();
		}
		else if (Actor->GetClass()->ImplementsInterface(UContextualAnimActorInterface::StaticClass()))
		{
			SkelMeshComp = IContextualAnimActorInterface::Execute_GetMesh(Actor);
		}
		else
		{
			SkelMeshComp = Actor->FindComponentByClass<USkeletalMeshComponent>();
		}
	}

	return SkelMeshComp;
}

UAnimInstance* UContextualAnimUtilities::TryGetAnimInstance(const AActor* Actor)
{
	if (USkeletalMeshComponent* SkelMeshComp = UContextualAnimUtilities::TryGetSkeletalMeshComponent(Actor))
	{
		return SkelMeshComp->GetAnimInstance();
	}

	return nullptr;
}

FAnimMontageInstance* UContextualAnimUtilities::TryGetActiveAnimMontageInstance(const AActor* Actor)
{
	if(UAnimInstance* AnimInstance = UContextualAnimUtilities::TryGetAnimInstance(Actor))
	{
		return AnimInstance->GetActiveMontageInstance();
	}

	return nullptr;
}

void UContextualAnimUtilities::BP_Montage_GetSectionStartAndEndTime(const UAnimMontage* Montage, int32 SectionIndex, float& OutStartTime, float& OutEndTime)
{
	if(Montage)
	{
		Montage->GetSectionStartAndEndTime(SectionIndex, OutStartTime, OutEndTime);
	}
}

float UContextualAnimUtilities::BP_Montage_GetSectionTimeLeftFromPos(const UAnimMontage* Montage, float Position)
{
	//UAnimMontage::GetSectionTimeLeftFromPos is not const :(
	return Montage ? (const_cast<UAnimMontage*>(Montage))->GetSectionTimeLeftFromPos(Position) : -1.f;
}

float UContextualAnimUtilities::BP_Montage_GetSectionLength(const UAnimMontage* Montage, int32 SectionIndex)
{
	return Montage ? Montage->GetSectionLength(SectionIndex) : -1.f;
}

void UContextualAnimUtilities::DrawSector(FPrimitiveDrawInterface& PDI, const FVector& Origin, const FVector& Direction, float MinDistance, float MaxDistance, float MinAngle, float MaxAngle, const FLinearColor& Color, uint8 DepthPriority, float Thickness)
{
	if(MinAngle == 0 && MaxAngle == 0)
	{
		DrawCircle(&PDI, Origin, FVector(1, 0, 0), FVector(0, 1, 0), Color, 30.f, 12, SDPG_World, 1.f);
		return;
	}

	// Draw Cone lines
	const FVector LeftDirection = Direction.RotateAngleAxis(MinAngle, FVector::UpVector);
	const FVector RightDirection = Direction.RotateAngleAxis(MaxAngle, FVector::UpVector);
	PDI.DrawLine(Origin + (LeftDirection * MinDistance), Origin + (LeftDirection * MaxDistance), Color, DepthPriority, Thickness);
	PDI.DrawLine(Origin + (RightDirection * MinDistance), Origin + (RightDirection * MaxDistance), Color, DepthPriority, Thickness);

	// Draw Near Arc
	FVector LastDirection = LeftDirection;
	float Angle = MinAngle;
	while (Angle < MaxAngle)
	{
		Angle = FMath::Clamp<float>(Angle + 10, MinAngle, MaxAngle);

		const float Length = MinDistance;
		const FVector NewDirection = Direction.RotateAngleAxis(Angle, FVector::UpVector);
		const FVector LineStart = Origin + (LastDirection * Length);
		const FVector LineEnd = Origin + (NewDirection * Length);
		PDI.DrawLine(LineStart, LineEnd, Color, DepthPriority, Thickness);
		LastDirection = NewDirection;
	}

	// Draw Far Arc
	LastDirection = LeftDirection;
	Angle = MinAngle;
	while (Angle < MaxAngle)
	{
		Angle = FMath::Clamp<float>(Angle + 10, MinAngle, MaxAngle);

		const float Length = MaxDistance;
		const FVector NewDirection = Direction.RotateAngleAxis(Angle, FVector::UpVector);
		const FVector LineStart = Origin + (LastDirection * Length);
		const FVector LineEnd = Origin + (NewDirection * Length);
		PDI.DrawLine(LineStart, LineEnd, Color, DepthPriority, Thickness);
		LastDirection = NewDirection;
	}
}