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
TAutoConsoleVariable<int32> FMotionWarpingCVars::CVarMotionWarpingDebug(TEXT("a.MotionWarping.Debug"), 0, TEXT("0: Disable, 1: Only Log 2: Log and DrawDebug"), ECVF_Cheat);
TAutoConsoleVariable<float> FMotionWarpingCVars::CVarMotionWarpingDrawDebugDuration(TEXT("a.MotionWarping.DrawDebugLifeTime"), 0.f, TEXT("Time in seconds each draw debug persists.\nRequires 'a.MotionWarping.Debug 2'"), ECVF_Cheat);
#endif

// UMotionWarpingUtilities
///////////////////////////////////////////////////////////////////////

void UMotionWarpingUtilities::BreakMotionWarpingSyncPoint(const FMotionWarpingSyncPoint& SyncPoint, FVector& Location, FRotator& Rotation)
{
	Location = SyncPoint.GetLocation(); 
	Rotation = SyncPoint.Rotator();
}

FMotionWarpingSyncPoint UMotionWarpingUtilities::MakeMotionWarpingSyncPoint(FVector Location, FRotator Rotation)
{
	return FMotionWarpingSyncPoint(Location, Rotation);
}

void UMotionWarpingUtilities::ExtractLocalSpacePose(const UAnimSequenceBase* Animation, const FBoneContainer& BoneContainer, float Time, bool bExtractRootMotion, FCompactPose& OutPose)
{
	OutPose.SetBoneContainer(&BoneContainer);

	FBlendedCurve Curve;
	Curve.InitFrom(BoneContainer);

	FAnimExtractContext Context(Time, bExtractRootMotion);

	FStackCustomAttributes Attributes;
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

bool UMotionWarpingComponent::ContainsMatchingModifier(const URootMotionModifierConfig* Config, const UAnimSequenceBase* Animation, float StartTime, float EndTime) const
{
	if(Config)
	{
		return RootMotionModifiers.ContainsByPredicate([=](const TUniquePtr<FRootMotionModifier>& Modifier)
		{
			return Config->MatchesConfig(Modifier, Animation, StartTime, EndTime);
		});
	}

	return false;
}

void UMotionWarpingComponent::Update()
{
	check(GetCharacterOwner());

	auto TryGetConfigFromNotifyEvent = [](const FAnimNotifyEvent& NotifyEvent)
	{
		const UAnimNotifyState_MotionWarping* MotionWarpingNotify = NotifyEvent.NotifyStateClass ? Cast<UAnimNotifyState_MotionWarping>(NotifyEvent.NotifyStateClass) : nullptr;
		return MotionWarpingNotify ? MotionWarpingNotify->RootMotionModifierConfig : nullptr;
	};

	const FAnimMontageInstance* RootMotionMontageInstance = GetCharacterOwner()->GetRootMotionAnimMontageInstance();
	const UAnimMontage* Montage = RootMotionMontageInstance ? RootMotionMontageInstance->Montage : nullptr;
	if (Montage)
	{
		const float PreviousPosition = RootMotionMontageInstance->GetPreviousPosition();
		const float CurrentPosition = RootMotionMontageInstance->GetPosition();

		// Loop over notifies directly in the montage, looking for Motion Warping windows
		for (const FAnimNotifyEvent& NotifyEvent : Montage->Notifies)
		{
			if (const URootMotionModifierConfig* MotionWarpingConfig = TryGetConfigFromNotifyEvent(NotifyEvent))
			{
				const float StartTime = FMath::Clamp(NotifyEvent.GetTriggerTime(), 0.f, Montage->GetPlayLength());
				const float EndTime = FMath::Clamp(NotifyEvent.GetEndTriggerTime(), 0.f, Montage->GetPlayLength());

				// If we are hitting this window...
				if (PreviousPosition >= StartTime && PreviousPosition < EndTime)
				{
					if (!ContainsMatchingModifier(MotionWarpingConfig, Montage, StartTime, EndTime))
					{
						RootMotionModifiers.Add(MotionWarpingConfig->CreateRootMotionModifier(Montage, StartTime, EndTime));

						UE_LOG(LogMotionWarping, Verbose, TEXT("UMotionWarpingComponent::UpdateModifiers Adding RootMotionModifier %s. NetMode: %d WorldTime: %f Animation: %s [%f %f] [%f %f] Loc: %s Rot: %s"),
							*GetNameSafe(MotionWarpingConfig), GetWorld()->GetNetMode(), GetWorld()->GetTimeSeconds(), *GetNameSafe(Montage), StartTime, EndTime, PreviousPosition, CurrentPosition, 
							*GetCharacterOwner()->GetActorLocation().ToString(), *GetCharacterOwner()->GetActorRotation().ToCompactString());
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
						if (const URootMotionModifierConfig* MotionWarpingConfig = TryGetConfigFromNotifyEvent(NotifyEvent))
						{
							const float NotifyStartTime = FMath::Clamp(NotifyEvent.GetTriggerTime(), 0.f, AnimSegment->AnimReference->GetPlayLength());
							const float NotifyEndTime = FMath::Clamp(NotifyEvent.GetEndTriggerTime(), 0.f, AnimSegment->AnimReference->GetPlayLength());

							// Convert notify times from AnimSequence times to montage times
							const float StartTime = (NotifyStartTime - AnimSegment->AnimStartTime) + AnimSegment->StartPos;
							const float EndTime = (NotifyEndTime - AnimSegment->AnimStartTime) + AnimSegment->StartPos;

							if (PreviousPosition >= StartTime && PreviousPosition < EndTime)
							{
								if (!ContainsMatchingModifier(MotionWarpingConfig, Montage, StartTime, EndTime))
								{
									RootMotionModifiers.Add(MotionWarpingConfig->CreateRootMotionModifier(Montage, StartTime, EndTime));

									UE_LOG(LogMotionWarping, Verbose, TEXT("UMotionWarpingComponent::UpdateModifiers Adding RootMotionModifier %s. NetMode: %d WorldTime: %f Animation: %s [%f %f] [%f %f] Loc: %s Rot: %s"),
										*GetNameSafe(MotionWarpingConfig), GetWorld()->GetNetMode(), GetWorld()->GetTimeSeconds(), *GetNameSafe(Montage), StartTime, EndTime, PreviousPosition, CurrentPosition,
										*GetCharacterOwner()->GetActorLocation().ToString(), *GetCharacterOwner()->GetActorRotation().ToCompactString());
								}
							}
						}
					}
				}
			}
		}
	}

	// Update the state of all the modifiers
	for (TUniquePtr<FRootMotionModifier>& Modifier : RootMotionModifiers)
	{
		Modifier->Update(*this);
	}

	// Remove the modifiers that has been marked for removal
	const int32 TotalRemoved = RootMotionModifiers.RemoveAll([](const TUniquePtr<FRootMotionModifier>& Modifier) { return Modifier->State == ERootMotionModifierState::MarkedForRemoval; });

	UE_CLOG(TotalRemoved > 0, LogMotionWarping, Verbose, TEXT("UMotionWarpingComponent::UpdateModifiers Modifiers removed after update: %d"), TotalRemoved);
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
	for (TUniquePtr<FRootMotionModifier>& RootMotionModifier : RootMotionModifiers)
	{
		if (RootMotionModifier->State == ERootMotionModifierState::Active && RootMotionModifier->bInLocalSpace)
		{
			FinalRootMotion = RootMotionModifier->ProcessRootMotion(*this, FinalRootMotion, DeltaSeconds);
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
	for (TUniquePtr<FRootMotionModifier>& RootMotionModifier : RootMotionModifiers)
	{
		if (RootMotionModifier->State == ERootMotionModifierState::Active && !RootMotionModifier->bInLocalSpace)
		{
			FinalRootMotion = RootMotionModifier->ProcessRootMotion(*this, FinalRootMotion, DeltaSeconds);
		}
	}

	return FinalRootMotion;
}

void UMotionWarpingComponent::AddOrUpdateSyncPoint(FName Name, const FMotionWarpingSyncPoint& SyncPoint)
{
	if (Name != NAME_None)
	{
		FMotionWarpingSyncPoint& MotionWarpingSyncPoint = SyncPoints.FindOrAdd(Name);
		MotionWarpingSyncPoint = SyncPoint;
	}
}