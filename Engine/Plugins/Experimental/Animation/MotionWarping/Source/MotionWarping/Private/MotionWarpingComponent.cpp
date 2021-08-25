// Copyright Epic Games, Inc. All Rights Reserved.

#include "MotionWarpingComponent.h"

#include "RootMotionModifier.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimationPoseData.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AnimNotifyState_MotionWarping.h"

DEFINE_LOG_CATEGORY(LogMotionWarping);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
TAutoConsoleVariable<int32> FMotionWarpingCVars::CVarMotionWarpingDisable(TEXT("a.MotionWarping.Disable"), 0, TEXT("Disable Motion Warping"), ECVF_Cheat);
TAutoConsoleVariable<int32> FMotionWarpingCVars::CVarMotionWarpingDebug(TEXT("a.MotionWarping.Debug"), 0, TEXT("0: Disable, 1: Only Log, 2: Only DrawDebug, 3: Log and DrawDebug"), ECVF_Cheat);
TAutoConsoleVariable<float> FMotionWarpingCVars::CVarMotionWarpingDrawDebugDuration(TEXT("a.MotionWarping.DrawDebugLifeTime"), 1.f, TEXT("Time in seconds each draw debug persists.\nRequires 'a.MotionWarping.Debug 2'"), ECVF_Cheat);
#endif

// UMotionWarpingUtilities
///////////////////////////////////////////////////////////////////////

void UMotionWarpingUtilities::ExtractLocalSpacePose(const UAnimSequenceBase* Animation, const FBoneContainer& BoneContainer, float Time, bool bExtractRootMotion, FCompactPose& OutPose)
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

void UMotionWarpingUtilities::ExtractComponentSpacePose(const UAnimSequenceBase* Animation, const FBoneContainer& BoneContainer, float Time, bool bExtractRootMotion, FCSPose<FCompactPose>& OutPose)
{
	FCompactPose Pose;
	ExtractLocalSpacePose(Animation, BoneContainer, Time, bExtractRootMotion, Pose);
	OutPose.InitPose(MoveTemp(Pose));
}

FTransform UMotionWarpingUtilities::ExtractRootMotionFromAnimation(const UAnimSequenceBase* Animation, float StartTime, float EndTime)
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

FTransform UMotionWarpingUtilities::ExtractRootTransformFromAnimation(const UAnimSequenceBase* Animation, float Time)
{
	if (const UAnimMontage* AnimMontage = Cast<UAnimMontage>(Animation))
	{
		if(const FAnimSegment* Segment = AnimMontage->SlotAnimTracks[0].AnimTrack.GetSegmentAtTime(Time))
		{
			if (const UAnimSequence* AnimSequence = Cast<UAnimSequence>(Segment->AnimReference))
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

void UMotionWarpingUtilities::GetMotionWarpingWindowsFromAnimation(const UAnimSequenceBase* Animation, TArray<FMotionWarpingWindowData>& OutWindows)
{
	if(Animation)
	{
		OutWindows.Reset();

		for (int32 Idx = 0; Idx < Animation->Notifies.Num(); Idx++)
		{
			const FAnimNotifyEvent& NotifyEvent = Animation->Notifies[Idx];
			if (UAnimNotifyState_MotionWarping* Notify = Cast<UAnimNotifyState_MotionWarping>(NotifyEvent.NotifyStateClass))
			{
				FMotionWarpingWindowData Data;
				Data.AnimNotify = Notify;
				Data.StartTime = NotifyEvent.GetTriggerTime();
				Data.EndTime = NotifyEvent.GetEndTriggerTime();
				OutWindows.Add(Data);
			}
		}
	}
}

void UMotionWarpingUtilities::GetMotionWarpingWindowsForWarpTargetFromAnimation(const UAnimSequenceBase* Animation, FName WarpTargetName, TArray<FMotionWarpingWindowData>& OutWindows)
{
	if (Animation && WarpTargetName != NAME_None)
	{
		OutWindows.Reset();

		for (int32 Idx = 0; Idx < Animation->Notifies.Num(); Idx++)
		{
			const FAnimNotifyEvent& NotifyEvent = Animation->Notifies[Idx];
			if (UAnimNotifyState_MotionWarping* Notify = Cast<UAnimNotifyState_MotionWarping>(NotifyEvent.NotifyStateClass))
			{
				if (const URootMotionModifier_Warp* Modifier = Cast<const URootMotionModifier_Warp>(Notify->RootMotionModifier))
				{
					if(Modifier->WarpTargetName == WarpTargetName)
					{
						FMotionWarpingWindowData Data;
						Data.AnimNotify = Notify;
						Data.StartTime = NotifyEvent.GetTriggerTime();
						Data.EndTime = NotifyEvent.GetEndTriggerTime();
						OutWindows.Add(Data);
					}
				}
			}
		}
	}
}

// UMotionWarpingComponent
///////////////////////////////////////////////////////////////////////

UMotionWarpingComponent::UMotionWarpingComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bWantsInitializeComponent = true;
}

void UMotionWarpingComponent::InitializeComponent()
{
	Super::InitializeComponent();

	CharacterOwner = Cast<ACharacter>(GetOwner());

	UCharacterMovementComponent* CharacterMovementComp = CharacterOwner.IsValid() ? CharacterOwner->GetCharacterMovement() : nullptr;
	if (CharacterMovementComp)
	{
		CharacterMovementComp->ProcessRootMotionPreConvertToWorld.BindUObject(this, &UMotionWarpingComponent::ProcessRootMotionPreConvertToWorld);
		CharacterMovementComp->ProcessRootMotionPostConvertToWorld.BindUObject(this, &UMotionWarpingComponent::ProcessRootMotionPostConvertToWorld);
	}
}

bool UMotionWarpingComponent::ContainsModifier(const UAnimSequenceBase* Animation, float StartTime, float EndTime) const
{
	return Modifiers.ContainsByPredicate([=](const URootMotionModifier* Modifier)
		{
			return (Modifier->Animation == Animation && Modifier->StartTime == StartTime && Modifier->EndTime == EndTime);
		});
}

int32 UMotionWarpingComponent::AddModifier(URootMotionModifier* Modifier)
{
	if (ensureAlways(Modifier))
	{
		UE_LOG(LogMotionWarping, Verbose, TEXT("MotionWarping: RootMotionModifier added. NetMode: %d WorldTime: %f Char: %s Animation: %s [%f %f] [%f %f] Loc: %s Rot: %s"),
			GetWorld()->GetNetMode(), GetWorld()->GetTimeSeconds(), *GetNameSafe(GetCharacterOwner()), *GetNameSafe(Modifier->Animation.Get()), Modifier->StartTime, Modifier->EndTime, Modifier->PreviousPosition, Modifier->CurrentPosition,
			*GetCharacterOwner()->GetActorLocation().ToString(), *GetCharacterOwner()->GetActorRotation().ToCompactString());

		return Modifiers.Add(Modifier);
	}

	return INDEX_NONE;
}

void UMotionWarpingComponent::DisableAllRootMotionModifiers()
{
	if (Modifiers.Num() > 0)
	{
		for (URootMotionModifier* Modifier : Modifiers)
		{
			Modifier->SetState(ERootMotionModifierState::Disabled);
		}
	}
}

void UMotionWarpingComponent::Update()
{
	check(GetCharacterOwner());

	const FAnimMontageInstance* RootMotionMontageInstance = GetCharacterOwner()->GetRootMotionAnimMontageInstance();
	UAnimMontage* Montage = RootMotionMontageInstance ? ToRawPtr(RootMotionMontageInstance->Montage) : nullptr;
	if (Montage)
	{
		const float PreviousPosition = RootMotionMontageInstance->GetPreviousPosition();
		const float CurrentPosition = RootMotionMontageInstance->GetPosition();

		// Loop over notifies directly in the montage, looking for Motion Warping windows
		for (const FAnimNotifyEvent& NotifyEvent : Montage->Notifies)
		{
			const UAnimNotifyState_MotionWarping* MotionWarpingNotify = NotifyEvent.NotifyStateClass ? Cast<UAnimNotifyState_MotionWarping>(NotifyEvent.NotifyStateClass) : nullptr;
			if (MotionWarpingNotify)
			{
				if(MotionWarpingNotify->RootMotionModifier == nullptr)
				{
					UE_LOG(LogMotionWarping, Warning, TEXT("MotionWarpingComponent::Update. A motion warping window in %s doesn't have a valid root motion modifier!"), *GetNameSafe(Montage));
					continue;
				}

				const float StartTime = FMath::Clamp(NotifyEvent.GetTriggerTime(), 0.f, Montage->GetPlayLength());
				const float EndTime = FMath::Clamp(NotifyEvent.GetEndTriggerTime(), 0.f, Montage->GetPlayLength());

				if (PreviousPosition >= StartTime && PreviousPosition < EndTime)
				{
					if (!ContainsModifier(Montage, StartTime, EndTime))
					{
						MotionWarpingNotify->OnBecomeRelevant(this, Montage, StartTime, EndTime);
					}
				}
			}
		}

		if(bSearchForWindowsInAnimsWithinMontages)
		{
			// Same as before but scanning all animation within the montage
			for (int32 SlotIdx = 0; SlotIdx < Montage->SlotAnimTracks.Num(); SlotIdx++)
			{
				const FAnimTrack& AnimTrack = Montage->SlotAnimTracks[SlotIdx].AnimTrack;
				const FAnimSegment* AnimSegment = AnimTrack.GetSegmentAtTime(PreviousPosition);
				if (AnimSegment && AnimSegment->AnimReference)
				{
					for (const FAnimNotifyEvent& NotifyEvent : AnimSegment->AnimReference->Notifies)
					{
						const UAnimNotifyState_MotionWarping* MotionWarpingNotify = NotifyEvent.NotifyStateClass ? Cast<UAnimNotifyState_MotionWarping>(NotifyEvent.NotifyStateClass) : nullptr;
						if (MotionWarpingNotify)
						{
							if (MotionWarpingNotify->RootMotionModifier == nullptr)
							{
								UE_LOG(LogMotionWarping, Warning, TEXT("MotionWarpingComponent::Update. A motion warping window in %s doesn't have a valid root motion modifier!"), *GetNameSafe(AnimSegment->AnimReference));
								continue;
							}

							const float NotifyStartTime = FMath::Clamp(NotifyEvent.GetTriggerTime(), 0.f, AnimSegment->AnimReference->GetPlayLength());
							const float NotifyEndTime = FMath::Clamp(NotifyEvent.GetEndTriggerTime(), 0.f, AnimSegment->AnimReference->GetPlayLength());

							// Convert notify times from AnimSequence times to montage times
							const float StartTime = (NotifyStartTime - AnimSegment->AnimStartTime) + AnimSegment->StartPos;
							const float EndTime = (NotifyEndTime - AnimSegment->AnimStartTime) + AnimSegment->StartPos;

							if (PreviousPosition >= StartTime && PreviousPosition < EndTime)
							{
								if (!ContainsModifier(Montage, StartTime, EndTime))
								{
									MotionWarpingNotify->OnBecomeRelevant(this, Montage, StartTime, EndTime);
								}
							}
						}
					}
				}
			}
		}
	}

	OnPreUpdate.Broadcast(this);

	// Update the state of all the modifiers
	if (Modifiers.Num() > 0)
	{
		for (URootMotionModifier* Modifier : Modifiers)
		{
			Modifier->Update();
		}

		// Remove the modifiers that has been marked for removal
		Modifiers.RemoveAll([this](const URootMotionModifier* Modifier)
		{
			if (Modifier->GetState() == ERootMotionModifierState::MarkedForRemoval)
			{
				UE_LOG(LogMotionWarping, Verbose, TEXT("MotionWarping: RootMotionModifier removed. NetMode: %d WorldTime: %f Char: %s Animation: %s [%f %f] [%f %f] Loc: %s Rot: %s"),
					GetWorld()->GetNetMode(), GetWorld()->GetTimeSeconds(), *GetNameSafe(GetCharacterOwner()), *GetNameSafe(Modifier->Animation.Get()), Modifier->StartTime, Modifier->EndTime, Modifier->PreviousPosition, Modifier->CurrentPosition,
					*GetCharacterOwner()->GetActorLocation().ToString(), *GetCharacterOwner()->GetActorRotation().ToCompactString());

				return true;
			}

			return false;
		});
	}
}

FTransform UMotionWarpingComponent::ProcessRootMotionPreConvertToWorld(const FTransform& InRootMotion, UCharacterMovementComponent* CharacterMovementComponent, float DeltaSeconds)
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (FMotionWarpingCVars::CVarMotionWarpingDisable.GetValueOnGameThread() > 0)
	{
		return InRootMotion;
	}
#endif

	// Check for warping windows and update modifier states
	Update();

	FTransform FinalRootMotion = InRootMotion;

	// Apply Local Space Modifiers
	for (URootMotionModifier* Modifier : Modifiers)
	{
		if (Modifier->GetState() == ERootMotionModifierState::Active && Modifier->bInLocalSpace)
		{
			FinalRootMotion = Modifier->ProcessRootMotion(FinalRootMotion, DeltaSeconds);
		}
	}

	return FinalRootMotion;
}

FTransform UMotionWarpingComponent::ProcessRootMotionPostConvertToWorld(const FTransform& InRootMotion, UCharacterMovementComponent* CharacterMovementComponent, float DeltaSeconds)
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (FMotionWarpingCVars::CVarMotionWarpingDisable.GetValueOnGameThread() > 0)
	{
		return InRootMotion;
	}
#endif

	FTransform FinalRootMotion = InRootMotion;

	// Apply World Space Modifiers
	for (URootMotionModifier* Modifier : Modifiers)
	{
		if (Modifier->GetState() == ERootMotionModifierState::Active && !Modifier->bInLocalSpace)
		{
			FinalRootMotion = Modifier->ProcessRootMotion(FinalRootMotion, DeltaSeconds);
		}
	}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	const int32 DebugLevel = FMotionWarpingCVars::CVarMotionWarpingDebug.GetValueOnGameThread();
	if (DebugLevel >= 2)
	{
		const float DrawDebugDuration = FMotionWarpingCVars::CVarMotionWarpingDrawDebugDuration.GetValueOnGameThread();
		const float PointSize = 7.f;
		const FVector ActorFeetLocation = CharacterMovementComponent->GetActorFeetLocation();
		if (Modifiers.Num() > 0)
		{
			if (!OriginalRootMotionAccum.IsSet())
			{
				OriginalRootMotionAccum = ActorFeetLocation;
				WarpedRootMotionAccum = ActorFeetLocation;
			}

			OriginalRootMotionAccum = OriginalRootMotionAccum.GetValue() + InRootMotion.GetLocation();
			WarpedRootMotionAccum = WarpedRootMotionAccum.GetValue() + FinalRootMotion.GetLocation();

			DrawDebugPoint(GetWorld(), OriginalRootMotionAccum.GetValue(), PointSize, FColor::Red, false, DrawDebugDuration, 0);
			DrawDebugPoint(GetWorld(), WarpedRootMotionAccum.GetValue(), PointSize, FColor::Green, false, DrawDebugDuration, 0);
		}
		else
		{
			OriginalRootMotionAccum.Reset();
			WarpedRootMotionAccum.Reset();
		}

		DrawDebugPoint(GetWorld(), ActorFeetLocation, PointSize, FColor::Blue, false, DrawDebugDuration, 0);
	}
#endif

	return FinalRootMotion;
}

void UMotionWarpingComponent::AddOrUpdateWarpTarget(FName WarpTargetName, const FMotionWarpingTarget& WarpTarget)
{
	if (WarpTargetName != NAME_None)
	{
		FMotionWarpingTarget& WarpTargetRef = WarpTargetMap.FindOrAdd(WarpTargetName);
		WarpTargetRef = WarpTarget;
	}
}

int32 UMotionWarpingComponent::RemoveWarpTarget(FName WarpTargetName)
{
	return WarpTargetMap.Remove(WarpTargetName);
}

void UMotionWarpingComponent::AddOrUpdateWarpTargetFromTransform(FName WarpTargetName, FTransform TargetTransform)
{
	AddOrUpdateWarpTarget(WarpTargetName, TargetTransform);
}

void UMotionWarpingComponent::AddOrUpdateWarpTargetFromComponent(FName WarpTargetName, const USceneComponent* Component, FName BoneName, bool bFollowComponent)
{
	if (Component == nullptr)
	{
		UE_LOG(LogMotionWarping, Warning, TEXT("AddOrUpdateWarpTargetFromComponent has failed!. Reason: Invalid Component"));
		return;
	}

	AddOrUpdateWarpTarget(WarpTargetName, FMotionWarpingTarget(Component, BoneName, bFollowComponent));
}

URootMotionModifier* UMotionWarpingComponent::AddModifierFromTemplate(URootMotionModifier* Template, const UAnimSequenceBase* Animation, float StartTime, float EndTime)
{
	if (ensureAlways(Template))
	{
		FObjectDuplicationParameters Params(Template, this);
		URootMotionModifier* NewRootMotionModifier = CastChecked<URootMotionModifier>(StaticDuplicateObjectEx(Params));
		
		NewRootMotionModifier->Animation = Animation;
		NewRootMotionModifier->StartTime = StartTime;
		NewRootMotionModifier->EndTime = EndTime;

		AddModifier(NewRootMotionModifier);

		return NewRootMotionModifier;
	}

	return nullptr;
}