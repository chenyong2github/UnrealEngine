// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimSceneInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MotionWarpingComponent.h"
#include "ContextualAnimSceneAsset.h"
#include "ContextualAnimSceneActorComponent.h"
#include "Engine/World.h"

// UContextualAnimSceneInstance
//================================================================================================================

UContextualAnimSceneInstance::UContextualAnimSceneInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

}

UWorld* UContextualAnimSceneInstance::GetWorld()const
{
	return GetOuter() ? GetOuter()->GetWorld() : nullptr;
}

void UContextualAnimSceneInstance::Tick(const float DeltaTime)
{
	RemainingDuration -= DeltaTime;
	if (RemainingDuration <= 0.f)
	{
		OnSectionEndTimeReached.Broadcast(this);
		RemainingDuration = MAX_flt;
	}
}

bool UContextualAnimSceneInstance::IsActorInThisScene(const AActor* Actor) const
{
	return FindBindingByActor(Actor) != nullptr;
}

AActor* UContextualAnimSceneInstance::GetActorByRole(FName Role) const
{
	const FContextualAnimSceneBinding* Binding = FindBindingByRole(Role);
	return Binding ? Binding->GetActor() : nullptr;
}

UAnimMontage* UContextualAnimSceneInstance::PlayAnimation(UAnimInstance& AnimInstance, UAnimSequenceBase& Animation)
{
	if (UAnimMontage* AnimMontage = Cast<UAnimMontage>(&Animation))
	{
		const float Duration = AnimInstance.Montage_Play(AnimMontage, 1.f, EMontagePlayReturnType::MontageLength);
		return (Duration > 0.f) ? AnimMontage : nullptr;
	}
	else
	{
		//@TODO: Expose all these on the AnimTrack
		const FName SlotName = FName(TEXT("DefaultSlot"));
		const float BlendInTime = 0.25f;
		const float BlendOutTime = 0.25f;
		const float InPlayRate = 1.f;
		const int32 LoopCount = 1;
		const float BlendOutTriggerTime = -1.f;
		const float InTimeToStartMontageAt = 0.f;
		return AnimInstance.PlaySlotAnimationAsDynamicMontage(&Animation, SlotName, BlendInTime, BlendOutTime, InPlayRate, LoopCount, BlendOutTriggerTime, InTimeToStartMontageAt);
	}
}

float UContextualAnimSceneInstance::Join(FContextualAnimSceneBinding& Binding)
{
	float Duration = MIN_flt;

	AActor* Actor = Binding.GetActor();
	if (Actor == nullptr)
	{
		return Duration;
	}

	if (UAnimSequenceBase* Animation = Binding.GetAnimTrack().Animation)
	{
		if (UAnimInstance* AnimInstance = Binding.GetAnimInstance())
		{
			if (const UAnimMontage* Montage = PlayAnimation(*AnimInstance, *Animation))
			{
				AnimInstance->OnPlayMontageNotifyBegin.AddUniqueDynamic(this, &UContextualAnimSceneInstance::OnNotifyBeginReceived);
				AnimInstance->OnPlayMontageNotifyEnd.AddUniqueDynamic(this, &UContextualAnimSceneInstance::OnNotifyEndReceived);
				AnimInstance->OnMontageBlendingOut.AddUniqueDynamic(this, &UContextualAnimSceneInstance::OnMontageBlendingOut);

				const float AdjustedPlayRate = AnimInstance->Montage_GetPlayRate(Montage) * Montage->RateScale;				
				if (AdjustedPlayRate > 0.f)
				{
					Duration = (Montage->GetPlayLength() / AdjustedPlayRate);	
				}
				else
				{
					UE_LOG(LogContextualAnim, Warning, TEXT("Undesired playrate %.3f, using montage play length instead."), AdjustedPlayRate);
					Duration = Montage->GetPlayLength();
				}
			}
		}

		//@TODO: Temp, until we have a way to switch between movement mode using AnimNotifyState
		if (Binding.GetAnimTrack().bRequireFlyingMode)
		{
			if (UCharacterMovementComponent* CharacterMovementComp = Actor->FindComponentByClass<UCharacterMovementComponent>())
			{
				CharacterMovementComp->SetMovementMode(MOVE_Flying);
			}
		}
	}

	if (UMotionWarpingComponent* MotionWarpComp = Actor->FindComponentByClass<UMotionWarpingComponent>())
	{
		for (const FContextualAnimSetPivot& Pivot : AlignmentSectionToScenePivotList)
		{
			const float Time = Binding.GetAnimTrack().GetSyncTimeForWarpSection(Pivot.Name);
			const FTransform TransformRelativeToScenePivot = Binding.GetAnimTrack().AlignmentData.ExtractTransformAtTime(Pivot.Name, Time);
			const FTransform WarpTarget = TransformRelativeToScenePivot * Pivot.Transform;
			MotionWarpComp->AddOrUpdateWarpTargetFromTransform(Pivot.Name, WarpTarget);
		}
	}

	if (SceneAsset->GetDisableCollisionBetweenActors())
	{
		SetIgnoreCollisionWithOtherActors(Actor, true);
	}

	Binding.SceneInstancePtr = this;

	if (UContextualAnimSceneActorComponent* SceneActorComp = Binding.GetSceneActorComponent())
	{
		SceneActorComp->OnJoinedScene(&Binding);
	}

	OnActorJoined.Broadcast(this, Actor);

	return Duration;
}

void UContextualAnimSceneInstance::Leave(FContextualAnimSceneBinding& Binding)
{
	if (UAnimInstance* AnimInstance = Binding.GetAnimInstance())
	{
		if (const UAnimMontage* CurrentMontage = AnimInstance->GetCurrentActiveMontage())
		{
			AnimInstance->Montage_Stop(CurrentMontage->BlendOut.GetBlendTime(), CurrentMontage);
		}
	}
}

bool UContextualAnimSceneInstance::TransitionTo(FContextualAnimSceneBinding& Binding, const FContextualAnimTrack& AnimTrack)
{
	check(AnimTrack.Animation != Binding.GetAnimTrack().Animation);
	check(AnimTrack.Role == Binding.GetRoleDef().Name);

	UAnimInstance* AnimInstance = Binding.GetAnimInstance();
	if (AnimInstance == nullptr)
	{		
		return false;
	}

	// Unbind blend out delegate for a moment so we don't get it during the transition
	// @TODO: Replace this with the TGuardValue 'pattern', similar to what we do in the editor for OnAnimNotifyChanged
	AnimInstance->OnMontageBlendingOut.RemoveDynamic(this, &UContextualAnimSceneInstance::OnMontageBlendingOut);

	PlayAnimation(*AnimInstance, *AnimTrack.Animation);
	Binding.AnimTrackPtr = &AnimTrack;

	AnimInstance->OnMontageBlendingOut.AddUniqueDynamic(this, &UContextualAnimSceneInstance::OnMontageBlendingOut);

	return true;
}

void UContextualAnimSceneInstance::Start()
{
	RemainingDuration = 0.f;

	for (auto& Binding : Bindings)
	{
		const float TrackDuration = Join(Binding);
		RemainingDuration = FMath::Max(RemainingDuration, TrackDuration);
	}
}

void UContextualAnimSceneInstance::Stop()
{
	for (auto& Binding : Bindings)
	{
		Leave(Binding);
	}
}

void UContextualAnimSceneInstance::OnMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted)
{
	UE_LOG(LogContextualAnim, Log, TEXT("UContextualAnimSceneInstance::OnMontageBlendingOut Montage: %s"), *GetNameSafe(Montage));

	for (auto& Binding : Bindings)
	{
		if (Binding.GetAnimTrack().Animation == Montage)
		{
			AActor* Actor = Binding.GetActor();
			if (UAnimInstance* AnimInstance = Binding.GetAnimInstance())
			{
				AnimInstance->OnPlayMontageNotifyBegin.RemoveDynamic(this, &UContextualAnimSceneInstance::OnNotifyBeginReceived);
				AnimInstance->OnPlayMontageNotifyEnd.RemoveDynamic(this, &UContextualAnimSceneInstance::OnNotifyEndReceived);
				AnimInstance->OnMontageBlendingOut.RemoveDynamic(this, &UContextualAnimSceneInstance::OnMontageBlendingOut);

				if (Binding.GetAnimTrack().bRequireFlyingMode)
				{
					if (UCharacterMovementComponent* CharacterMovementComp = Actor->FindComponentByClass<UCharacterMovementComponent>())
					{
						CharacterMovementComp->SetMovementMode(MOVE_Walking);
					}
				}
			}

			if (SceneAsset->GetDisableCollisionBetweenActors())
			{
				SetIgnoreCollisionWithOtherActors(Binding.GetActor(), false);
			}

			if (UContextualAnimSceneActorComponent* SceneActorComp = Binding.GetSceneActorComponent())
			{
				SceneActorComp->OnLeftScene(&Binding);
			}

			OnActorLeft.Broadcast(this, Actor);

			break;
		}
	}

	bool bShouldEnd = true;
	for (auto& Binding : Bindings)
	{
		if (UAnimInstance* AnimInstance = Binding.GetAnimInstance())
		{
			// Keep montage support for now but might go away soon
			if (const UAnimMontage* AnimMontage = Cast<UAnimMontage>(Binding.GetAnimTrack().Animation))
			{
				if (AnimInstance->Montage_IsPlaying(AnimMontage))
				{
					bShouldEnd = false;
					break;
				}
			}
			else
			{
				for (const FAnimMontageInstance* MontageInstance : AnimInstance->MontageInstances)
				{
					// When the animation is not a Montage, we still play it as a Montage. This dynamically created Montage has a single slot and single segment.
					if (MontageInstance && MontageInstance->IsPlaying())
					{
						if(MontageInstance->Montage->SlotAnimTracks.Num() > 0 && MontageInstance->Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() > 0)
						{
							if(MontageInstance->Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference() == Binding.GetAnimTrack().Animation)
							{
								bShouldEnd = false;
								break;
							}
						}
					}
				}
			}
		}
	}

	if (bShouldEnd)
	{
		OnSceneEnded.Broadcast(this);
		OnSectionDonePlaying.Broadcast(this);
	}
}

void UContextualAnimSceneInstance::OnNotifyBeginReceived(FName NotifyName, const FBranchingPointNotifyPayload& BranchingPointNotifyPayload)
{
	UE_LOG(LogContextualAnim, Log, TEXT("UContextualAnimSceneInstance::OnNotifyBeginReceived NotifyName: %s Montage: %s"),
	*NotifyName.ToString(), *GetNameSafe(BranchingPointNotifyPayload.SequenceAsset));

	if(const USkeletalMeshComponent* SkelMeshCom = BranchingPointNotifyPayload.SkelMeshComponent)
	{
		OnNotifyBegin.Broadcast(this, SkelMeshCom->GetOwner(), NotifyName);
	}
}

void UContextualAnimSceneInstance::OnNotifyEndReceived(FName NotifyName, const FBranchingPointNotifyPayload& BranchingPointNotifyPayload)
{
	UE_LOG(LogContextualAnim, Log, TEXT("UContextualAnimSceneInstance::OnNotifyEndReceived NotifyName: %s Montage: %s"),
	*NotifyName.ToString(), *GetNameSafe(BranchingPointNotifyPayload.SequenceAsset));

	if (const USkeletalMeshComponent* SkelMeshCom = BranchingPointNotifyPayload.SkelMeshComponent)
	{
		OnNotifyEnd.Broadcast(this, SkelMeshCom->GetOwner(), NotifyName);
	}
}

void UContextualAnimSceneInstance::SetIgnoreCollisionWithOtherActors(AActor* Actor, bool bValue) const
{
	for (auto& Binding : Bindings)
	{
		AActor* OtherActor = Binding.GetActor();
		if(OtherActor != Actor)
		{
			if (UPrimitiveComponent* RootPrimitiveComponent = Cast<UPrimitiveComponent>(Actor->GetRootComponent()))
			{
				RootPrimitiveComponent->IgnoreActorWhenMoving(OtherActor, bValue);
			}
		}
	}
}