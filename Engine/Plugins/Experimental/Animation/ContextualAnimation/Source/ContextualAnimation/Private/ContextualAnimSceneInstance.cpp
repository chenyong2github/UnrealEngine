// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimSceneInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MotionWarpingComponent.h"
#include "ContextualAnimActorInterface.h"
#include "ContextualAnimSceneAsset.h"
#include "ContextualAnimSceneActorComponent.h"
#include "ContextualAnimUtilities.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

UContextualAnimSceneActorComponent* FContextualAnimSceneActorData::GetSceneActorComponent() const
{
	//@TODO: Cache this during the binding
	return (Actor.IsValid()) ? Actor->FindComponentByClass<UContextualAnimSceneActorComponent>() : nullptr;
}

FTransform FContextualAnimSceneActorData::GetTransform() const
{
	const UContextualAnimSceneActorComponent* Comp = GetSceneActorComponent();
	return Comp ? Comp->GetComponentTransform() : Actor->GetActorTransform();
}

UAnimInstance* FContextualAnimSceneActorData::GetAnimInstance() const
{
	return UContextualAnimUtilities::TryGetAnimInstance(GetActor());
}

USkeletalMeshComponent* FContextualAnimSceneActorData::GetSkeletalMeshComponent() const
{
	return UContextualAnimUtilities::TryGetSkeletalMeshComponent(GetActor());
}

FAnimMontageInstance* FContextualAnimSceneActorData::GetAnimMontageInstance() const
{
	if (UAnimInstance* AnimInstance = GetAnimInstance())
	{
		return AnimInstance->GetActiveMontageInstance();
	}

	return nullptr;
}

float FContextualAnimSceneActorData::GetAnimTime() const
{
	const FAnimMontageInstance* MontageInstance = GetAnimMontageInstance();
	return MontageInstance ? MontageInstance->GetPosition() : -1.f;
}

FName FContextualAnimSceneActorData::GetCurrentSection() const
{
	const FAnimMontageInstance* MontageInstance = GetAnimMontageInstance();
	return MontageInstance ? MontageInstance->GetCurrentSection() : NAME_None;
}

int32 FContextualAnimSceneActorData::GetCurrentSectionIndex() const
{
	if(const FAnimMontageInstance* MontageInstance = GetAnimMontageInstance())
	{
		float CurrentPosition;
		return MontageInstance->Montage->GetAnimCompositeSectionIndexFromPos(MontageInstance->GetPosition(), CurrentPosition);
	}

	return INDEX_NONE;
}

const FContextualAnimIKTargetDefContainer& FContextualAnimSceneActorData::GetIKTargetDefs() const
{ 
	return GetSceneInstance().GetSceneAsset().GetIKTargetDefsForRole(Role); 
}

//================================================================================================================

void UContextualAnimSceneInstance::BreakContextualAnimSceneActorData(const FContextualAnimSceneActorData& SceneActorData, AActor*& Actor, UAnimMontage*& Montage, float& AnimTime, int32& CurrentSectionIndex, FName& CurrentSectionName)
{
	Actor = SceneActorData.GetActor();
	Montage = Cast<UAnimMontage>(SceneActorData.GetAnimTrack().Animation);
	AnimTime = SceneActorData.GetAnimTime();
	CurrentSectionIndex = SceneActorData.GetCurrentSectionIndex();
	CurrentSectionName = SceneActorData.GetCurrentSection();
}

//================================================================================================================

UContextualAnimSceneInstance::UContextualAnimSceneInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

}

UWorld* UContextualAnimSceneInstance::GetWorld()const
{
	return GetOuter() ? GetOuter()->GetWorld() : nullptr;
}

void UContextualAnimSceneInstance::Tick(float DeltaTime)
{
}

bool UContextualAnimSceneInstance::IsActorInThisScene(const AActor* Actor) const
{
	return FindSceneActorDataByActor(Actor) != nullptr;
}

AActor* UContextualAnimSceneInstance::GetActorByRole(FName Role) const
{
	const FContextualAnimSceneActorData* SceneActorData = FindSceneActorDataByRole(Role);
	return SceneActorData ? SceneActorData->GetActor() : nullptr;
}

void UContextualAnimSceneInstance::Join(FContextualAnimSceneActorData& SceneActorData)
{
	AActor* Actor = SceneActorData.GetActor();
	if (Actor == nullptr)
	{
		return;
	}

	if (UAnimSequenceBase* Animation = SceneActorData.GetAnimTrack().Animation)
	{
		if (UAnimInstance* AnimInstance = SceneActorData.GetAnimInstance())
		{
			// Keep montage support for now but might go away soon
			if (UAnimMontage* AnimMontage = Cast<UAnimMontage>(Animation))
			{
				AnimInstance->Montage_Play(AnimMontage, 1.f, EMontagePlayReturnType::MontageLength, SceneActorData.GetAnimStartTime());
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
				const float InTimeToStartMontageAt = SceneActorData.GetAnimStartTime();
				AnimInstance->PlaySlotAnimationAsDynamicMontage(SceneActorData.GetAnimTrack().Animation, SlotName, BlendInTime, BlendOutTime, InPlayRate, LoopCount, BlendOutTriggerTime, InTimeToStartMontageAt);
			}

			AnimInstance->OnPlayMontageNotifyBegin.AddUniqueDynamic(this, &UContextualAnimSceneInstance::OnNotifyBeginReceived);
			AnimInstance->OnPlayMontageNotifyEnd.AddUniqueDynamic(this, &UContextualAnimSceneInstance::OnNotifyEndReceived);
			AnimInstance->OnMontageBlendingOut.AddUniqueDynamic(this, &UContextualAnimSceneInstance::OnMontageBlendingOut);
		}


		//@TODO: Temp, until we have a way to switch between movement mode using AnimNotifyState
		if (SceneActorData.GetAnimTrack().bRequireFlyingMode)
		{
			if (UCharacterMovementComponent* CharacterMovementComp = Actor->FindComponentByClass<UCharacterMovementComponent>())
			{
				CharacterMovementComp->SetMovementMode(MOVE_Flying);
			}
		}
	}

	if (UMotionWarpingComponent* MotionWarpComp = Actor->FindComponentByClass<UMotionWarpingComponent>())
	{
		for (const auto& Pair : AlignmentSectionToScenePivotList)
		{
			const FName WarpTargetName = Pair.Key;
			const FTransform ScenePivotRuntime = Pair.Value;
			const float Time = SceneActorData.GetAnimTrack().GetSyncTimeForWarpSection(WarpTargetName);
			const FTransform TransformRelativeToScenePivot = SceneActorData.GetAnimTrack().AlignmentData.ExtractTransformAtTime(WarpTargetName, Time);
			const FTransform WarpTarget = (TransformRelativeToScenePivot * ScenePivotRuntime);

			MotionWarpComp->AddOrUpdateWarpTargetFromTransform(WarpTargetName, WarpTarget);
		}
	}

	if(SceneAsset->GetDisableCollisionBetweenActors())
	{
		SetIgnoreCollisionWithOtherActors(Actor, true);
	}

	SceneActorData.SceneInstancePtr = this;

	if(UContextualAnimSceneActorComponent* SceneActorComp = SceneActorData.GetSceneActorComponent())
	{
		SceneActorComp->OnJoinedScene(&SceneActorData);
	}

	OnActorJoined.Broadcast(this, Actor);
}

void UContextualAnimSceneInstance::Leave(FContextualAnimSceneActorData& SceneActorData)
{
	// Check if we have an exit section and transition to it, otherwise just stop the montage
	static const FName ExitSectionName = FName(TEXT("Exit"));
	if(TransitionTo(SceneActorData, ExitSectionName) == false)
	{
		AActor* Actor = SceneActorData.GetActor();
		if (UAnimInstance* AnimInstance = SceneActorData.GetAnimInstance())
		{
			UAnimMontage* CurrentMontage = AnimInstance->GetCurrentActiveMontage();
			if (CurrentMontage)
			{
				AnimInstance->Montage_Stop(CurrentMontage->BlendOut.GetBlendTime(), CurrentMontage);
			}
		}
	}
}

bool UContextualAnimSceneInstance::TransitionTo(FContextualAnimSceneActorData& SceneActorData, const FName& ToSectionName)
{
	UAnimInstance* AnimInstance = SceneActorData.GetAnimInstance();
	if (AnimInstance == nullptr)
	{		
		return false;
	}

	UAnimMontage* CurrentMontage = AnimInstance->GetCurrentActiveMontage();
	if (CurrentMontage == nullptr)
	{
		UE_LOG(LogContextualAnim, Log, TEXT("UContextualAnimSceneInstance::TransitionTo. Actor is not playing any montage. Actor: %s ToSectionName: %s"),
			*GetNameSafe(SceneActorData.GetActor()), *ToSectionName.ToString());

		return false;
	}

	const int32 SectionIdx = CurrentMontage->GetSectionIndex(ToSectionName);
	if (SectionIdx == INDEX_NONE)
	{
		UE_LOG(LogContextualAnim, Log, TEXT("UContextualAnimSceneInstance::TransitionTo. Invalid Section. Actor: %s CurrentMontage: %s ToSectionName: %s"),
			*GetNameSafe(SceneActorData.GetActor()), *GetNameSafe(CurrentMontage), *ToSectionName.ToString());

		return false;
	}

	UE_LOG(LogContextualAnim, Verbose, TEXT("UContextualAnimSceneInstance::TransitionTo. Actor: %s CurrentMontage: %s ToSectionName: %s"),
		*GetNameSafe(SceneActorData.GetActor()), *GetNameSafe(CurrentMontage), *ToSectionName.ToString());

	// Unbind blend out delegate for a moment so we don't get it during the transition
	AnimInstance->OnMontageBlendingOut.RemoveDynamic(this, &UContextualAnimSceneInstance::OnMontageBlendingOut);

	AnimInstance->Montage_Play(CurrentMontage, 1.f);
	AnimInstance->Montage_JumpToSection(ToSectionName, CurrentMontage);

	AnimInstance->OnMontageBlendingOut.AddUniqueDynamic(this, &UContextualAnimSceneInstance::OnMontageBlendingOut);

	return true;
}

void UContextualAnimSceneInstance::Start()
{
	for (const FContextualAnimAlignmentSectionData AligmentData : SceneAsset->GetAlignmentSections())
	{
		FTransform ScenePivotRuntime = FTransform::Identity;
		if (const FContextualAnimSceneActorData* SceneActor = FindSceneActorDataByRole(AligmentData.Origin))
		{
			if (AligmentData.bAlongClosestDistance)
			{
				if (const FContextualAnimSceneActorData* OtherSceneActor = FindSceneActorDataByRole(AligmentData.OtherRole))
				{
					const FTransform T1 = SceneActor->GetActor()->GetActorTransform();
					const FTransform T2 = OtherSceneActor->GetActor()->GetActorTransform();

					ScenePivotRuntime.SetLocation(FMath::Lerp<FVector>(T1.GetLocation(), T2.GetLocation(), AligmentData.Weight));
					ScenePivotRuntime.SetRotation((T2.GetLocation() - T1.GetLocation()).GetSafeNormal2D().ToOrientationQuat());
				}
			}
			else
			{
				ScenePivotRuntime = SceneActor->GetActor()->GetActorTransform();
			}
		}

		AlignmentSectionToScenePivotList.Add(MakeTuple(AligmentData.WarpTargetName, ScenePivotRuntime));
	}
	

	for (auto& Binding : Bindings)
	{
		Join(Binding);
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
			if (UAnimMontage* AnimMontage = Cast<UAnimMontage>(Binding.GetAnimTrack().Animation))
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

	if(bShouldEnd)
	{
		OnSceneEnded.Broadcast(this);
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