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

void UMotionWarpingUtilities::GetMotionWarpingWindowsForSyncPointFromAnimation(const UAnimSequenceBase* Animation, FName SyncPointName, TArray<FMotionWarpingWindowData>& OutWindows)
{
	if (Animation && SyncPointName != NAME_None)
	{
		OutWindows.Reset();

		for (int32 Idx = 0; Idx < Animation->Notifies.Num(); Idx++)
		{
			const FAnimNotifyEvent& NotifyEvent = Animation->Notifies[Idx];
			if (UAnimNotifyState_MotionWarping* Notify = Cast<UAnimNotifyState_MotionWarping>(NotifyEvent.NotifyStateClass))
			{
				if (const URootMotionModifierConfig_Warp* Config = Cast<const URootMotionModifierConfig_Warp>(Notify->RootMotionModifierConfig))
				{
					if(Config->SyncPointName == SyncPointName)
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
	return RootMotionModifiers.ContainsByPredicate([=](const TSharedPtr<FRootMotionModifier>& Modifier)
		{
			return (Modifier.IsValid() && Modifier->Animation == Animation && Modifier->StartTime == StartTime && Modifier->EndTime == EndTime);
		});
}

void UMotionWarpingComponent::AddRootMotionModifier(TSharedPtr<FRootMotionModifier> Modifier)
{
	if (ensureAlways(Modifier.IsValid()))
	{
		RootMotionModifiers.Add(Modifier);

		UE_LOG(LogMotionWarping, Verbose, TEXT("MotionWarping: RootMotionModifier added. NetMode: %d WorldTime: %f Char: %s Animation: %s [%f %f] [%f %f] Loc: %s Rot: %s"),
			GetWorld()->GetNetMode(), GetWorld()->GetTimeSeconds(), *GetNameSafe(GetCharacterOwner()), *GetNameSafe(Modifier->Animation.Get()), Modifier->StartTime, Modifier->EndTime, Modifier->PreviousPosition, Modifier->CurrentPosition,
			*GetCharacterOwner()->GetActorLocation().ToString(), *GetCharacterOwner()->GetActorRotation().ToCompactString());
	}
}

void UMotionWarpingComponent::DisableAllRootMotionModifiers()
{
	if (RootMotionModifiers.Num() > 0)
	{
		for (TSharedPtr<FRootMotionModifier>& Modifier : RootMotionModifiers)
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
				const float StartTime = FMath::Clamp(NotifyEvent.GetTriggerTime(), 0.f, Montage->GetPlayLength());
				const float EndTime = FMath::Clamp(NotifyEvent.GetEndTriggerTime(), 0.f, Montage->GetPlayLength());

				if (PreviousPosition >= StartTime && PreviousPosition < EndTime)
				{
					if (!ContainsModifier(Montage, StartTime, EndTime))
					{
						MotionWarpingNotify->AddRootMotionModifier(this, Montage, StartTime, EndTime);

						//@TODO: Temp hack to keep track of the AnimNotifyState each modifier is created from.
						if (RootMotionModifiers.Num())
						{
							TSharedPtr<FRootMotionModifier> Last = RootMotionModifiers.Last();
							if (Last.IsValid() && Last->Animation.Get() == Montage && Last->StartTime == StartTime && Last->EndTime == EndTime)
							{
								Last->AnimNotifyState = MotionWarpingNotify;
								Last->AnimNotifyState->OnWarpBegin(this, Last->Animation.Get(), Last->StartTime, Last->EndTime);
							}
						}
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
							const float NotifyStartTime = FMath::Clamp(NotifyEvent.GetTriggerTime(), 0.f, AnimSegment->AnimReference->GetPlayLength());
							const float NotifyEndTime = FMath::Clamp(NotifyEvent.GetEndTriggerTime(), 0.f, AnimSegment->AnimReference->GetPlayLength());

							// Convert notify times from AnimSequence times to montage times
							const float StartTime = (NotifyStartTime - AnimSegment->AnimStartTime) + AnimSegment->StartPos;
							const float EndTime = (NotifyEndTime - AnimSegment->AnimStartTime) + AnimSegment->StartPos;

							if (PreviousPosition >= StartTime && PreviousPosition < EndTime)
							{
								if (!ContainsModifier(Montage, StartTime, EndTime))
								{
									MotionWarpingNotify->AddRootMotionModifier(this, Montage, StartTime, EndTime);

									//@TODO: Temp hack to keep track of the AnimNotifyState each modifier is created from.
									if (RootMotionModifiers.Num())
									{
										TSharedPtr<FRootMotionModifier> Last = RootMotionModifiers.Last();
										if(Last.IsValid() && Last->Animation.Get() == Montage && Last->StartTime == StartTime && Last->EndTime == EndTime)
										{
											Last->AnimNotifyState = MotionWarpingNotify;
											Last->AnimNotifyState->OnWarpBegin(this, Last->Animation.Get(), Last->StartTime, Last->EndTime);
										}
									}
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
	if(RootMotionModifiers.Num() > 0)
	{
		for (TSharedPtr<FRootMotionModifier>& Modifier : RootMotionModifiers)
		{
			const UAnimNotifyState_MotionWarping* AnimNotify = Modifier->AnimNotifyState.Get();
			if (AnimNotify)
			{
				AnimNotify->OnWarpPreUpdate(this, Modifier->Animation.Get(), Modifier->StartTime, Modifier->EndTime);
			}

			Modifier->Update(*this);

			if(AnimNotify)
			{
				if(Modifier->GetState() == ERootMotionModifierState::Disabled || Modifier->GetState() == ERootMotionModifierState::MarkedForRemoval)
				{
					AnimNotify->OnWarpEnd(this, Modifier->Animation.Get(), Modifier->StartTime, Modifier->EndTime);
				}
			}
		}

		// Remove the modifiers that has been marked for removal
		RootMotionModifiers.RemoveAll([this](const TSharedPtr<FRootMotionModifier>& Modifier) 
		{ 
			if(Modifier->GetState() == ERootMotionModifierState::MarkedForRemoval)
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
	for (TSharedPtr<FRootMotionModifier>& RootMotionModifier : RootMotionModifiers)
	{
		if (RootMotionModifier->GetState() == ERootMotionModifierState::Active && RootMotionModifier->bInLocalSpace)
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
	for (TSharedPtr<FRootMotionModifier>& RootMotionModifier : RootMotionModifiers)
	{
		if (RootMotionModifier->GetState() == ERootMotionModifierState::Active && !RootMotionModifier->bInLocalSpace)
		{
			FinalRootMotion = RootMotionModifier->ProcessRootMotion(*this, FinalRootMotion, DeltaSeconds);
		}
	}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	const int32 DebugLevel = FMotionWarpingCVars::CVarMotionWarpingDebug.GetValueOnGameThread();
	if (DebugLevel >= 2)
	{
		const float DrawDebugDuration = FMotionWarpingCVars::CVarMotionWarpingDrawDebugDuration.GetValueOnGameThread();
		const float PointSize = 7.f;
		const FVector ActorFeetLocation = CharacterMovementComponent->GetActorFeetLocation();
		if (RootMotionModifiers.Num() > 0)
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

void UMotionWarpingComponent::AddOrUpdateSyncPoint(FName Name, const FMotionWarpingSyncPoint& SyncPoint)
{
	if (Name != NAME_None)
	{
		FMotionWarpingSyncPoint& MotionWarpingSyncPoint = SyncPoints.FindOrAdd(Name);
		MotionWarpingSyncPoint = SyncPoint;
	}
}

int32 UMotionWarpingComponent::RemoveSyncPoint(FName Name)
{
	return SyncPoints.Remove(Name);
}