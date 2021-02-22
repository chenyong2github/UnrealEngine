// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimSceneInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MotionWarpingComponent.h"
#include "ContextualAnimActorInterface.h"
#include "ContextualAnimScenePivotProvider.h"
#include "ContextualAnimSceneAsset.h"
#include "ContextualAnimSceneActorComponent.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

//@TODO: Move to ContextualAnimUtilities
static UAnimInstance* TryGetAnimInstance(AActor* Actor)
{
	UAnimInstance* AnimInstance = nullptr;
	if (Actor)
	{
		if (ACharacter* Character = Cast<ACharacter>(Actor))
		{
			USkeletalMeshComponent* SkelMeshComp = Character->GetMesh();
			AnimInstance = SkelMeshComp ? SkelMeshComp->GetAnimInstance() : nullptr;
		}
		else if (Actor->GetClass()->ImplementsInterface(UContextualAnimActorInterface::StaticClass()))
		{
			USkeletalMeshComponent* SkelMeshComp = IContextualAnimActorInterface::Execute_GetMesh(Actor);
			AnimInstance = SkelMeshComp ? SkelMeshComp->GetAnimInstance() : nullptr;
		}
		else if (USkeletalMeshComponent* SkelMeshComp = Actor->FindComponentByClass<USkeletalMeshComponent>())
		{
			AnimInstance = SkelMeshComp->GetAnimInstance();
		}
	}

	return AnimInstance;
}

//================================================================================================================

FTransform FContextualAnimSceneActorData::GetTransform() const
{
	FTransform Transform = FTransform::Identity;

	if(Actor.IsValid())
	{
		//@TODO: Cache this during the binding
		if (const UContextualAnimSceneActorComponent* Comp = Actor->FindComponentByClass<UContextualAnimSceneActorComponent>())
		{
			Transform = Comp->GetComponentTransform();
		}
		else
		{
			Transform = Actor->GetActorTransform();
		}
	}

	return Transform;
}

float FContextualAnimSceneActorData::GetAnimTime() const
{
	if(const UAnimMontage* Animation = AnimDataPtr->Animation)
	{
		if (UAnimInstance* AnimInstance = TryGetAnimInstance(Actor.Get()))
		{
			if(const FAnimMontageInstance* MontageInstance = AnimInstance->GetActiveInstanceForMontage(Animation))
			{
				return MontageInstance->GetPosition();
			}
		}
	}
	
	return 0.f;
}

//================================================================================================================

UContextualAnimSceneInstance::UContextualAnimSceneInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

}

UWorld* UContextualAnimSceneInstance::GetWorld()const
{
	return CastChecked<UWorld>(GetOuter()->GetWorld());
}

void UContextualAnimSceneInstance::Tick(float DeltaTime)
{
}

bool UContextualAnimSceneInstance::IsActorInThisScene(const AActor* Actor) const
{
	if(Actor)
	{
		for (const auto& Pair : SceneActorMap)
		{
			const FContextualAnimSceneActorData& Data = Pair.Value;
			if(Data.GetActor() == Actor)
			{
				return true;
			}
		}
	}

	return false;
}

void UContextualAnimSceneInstance::Join(FContextualAnimSceneActorData& Data)
{
	AActor* Actor = Data.GetActor();
	if (UAnimInstance* AnimInstance = TryGetAnimInstance(Actor))
	{
		if (UMotionWarpingComponent* MotionWarpingComp = Actor->FindComponentByClass<UMotionWarpingComponent>())
		{
			for (const auto& Pair : AlignmentSectionToScenePivotList)
			{
				const FName SyncPointName = Pair.Key;
				const float SyncTime = Data.AnimDataPtr->GetSyncTimeForWarpSection(SyncPointName);
				const FTransform AlignmentTransform = (Data.AnimDataPtr->AlignmentData.ExtractTransformAtTime(SyncPointName, SyncTime) * Pair.Value);
				MotionWarpingComp->AddOrUpdateSyncPoint(SyncPointName, AlignmentTransform);
			}
		}

		UAnimMontage* Animation = Data.AnimDataPtr->Animation;
		AnimInstance->Montage_Play(Animation, 1.f, EMontagePlayReturnType::MontageLength, Data.AnimStartTime);

		AnimInstance->OnPlayMontageNotifyBegin.AddUniqueDynamic(this, &UContextualAnimSceneInstance::OnNotifyBeginReceived);
		AnimInstance->OnPlayMontageNotifyEnd.AddUniqueDynamic(this, &UContextualAnimSceneInstance::OnNotifyEndReceived);
		AnimInstance->OnMontageBlendingOut.AddUniqueDynamic(this, &UContextualAnimSceneInstance::OnMontageBlendingOut);

		if (UCharacterMovementComponent* CharacterMovementComp = Actor->FindComponentByClass<UCharacterMovementComponent>())
		{
			CharacterMovementComp->SetMovementMode(MOVE_Flying);
		}
	}

	SetIgnoreCollisionWithOtherActors(Actor, true);

	OnActorJoined.ExecuteIfBound(this, Actor);
}

void UContextualAnimSceneInstance::Leave(FContextualAnimSceneActorData& Data)
{
	AActor* Actor = Data.GetActor();
	if (UAnimInstance* AnimInstance = TryGetAnimInstance(Actor))
	{
		UAnimMontage* CurrentMontage = AnimInstance->GetCurrentActiveMontage();
		check(CurrentMontage);

		// Check if we have an exit section and transition to it, otherwise just stop the montage
		// @TODO: This is temp until we add a solid way to deal with different states
		static const FName ExitSectionName = FName(TEXT("Exit"));
		const int32 SectionIdx = CurrentMontage->GetSectionIndex(ExitSectionName);
		if (SectionIdx != INDEX_NONE)
		{
			// Unbind blend out delegate for a moment so we don't get it during the transition
			AnimInstance->OnMontageBlendingOut.RemoveDynamic(this, &UContextualAnimSceneInstance::OnMontageBlendingOut);

			AnimInstance->Montage_Play(CurrentMontage, 1.f);
			AnimInstance->Montage_JumpToSection(ExitSectionName, CurrentMontage);

			AnimInstance->OnMontageBlendingOut.AddUniqueDynamic(this, &UContextualAnimSceneInstance::OnMontageBlendingOut);
		}
		else
		{
			AnimInstance->Montage_Stop(CurrentMontage->BlendOut.GetBlendTime(), CurrentMontage);
		}
	}
}

void UContextualAnimSceneInstance::Start()
{
	const int32 AlignmentSectionsNum = SceneAsset->AlignmentSections.Num();
	
	AlignmentSectionToScenePivotList.Reset(AlignmentSectionsNum);

	for (int32 Idx = 0; Idx < AlignmentSectionsNum; Idx++)
	{
		FTransform ScenePivot = FTransform::Identity;

		if(SceneAsset->AlignmentSections[Idx].ScenePivotProvider)
		{
			ScenePivot = SceneAsset->AlignmentSections[Idx].ScenePivotProvider->CalculateScenePivot_Runtime(SceneActorMap);
		}
		else
		{
			ScenePivot = SceneActorMap.FindRef(SceneAsset->PrimaryRole).GetTransform();
		}

		AlignmentSectionToScenePivotList.Add(MakeTuple(SceneAsset->AlignmentSections[Idx].SectionName, ScenePivot));

		//DrawDebugCoordinateSystem(GetWorld(), SceneOrigin.GetLocation(), SceneOrigin.Rotator(), 100.f, false, 10.f, 0, 5.f);
	}

	for (auto& Pair : SceneActorMap)
	{
		const FName& Role = Pair.Key;
		FContextualAnimSceneActorData& Data = Pair.Value;

		if (SceneAsset->GetJoinRuleForRole(Role) == EContextualAnimJoinRule::Default)
		{
			Join(Data);
		}
	}
}

void UContextualAnimSceneInstance::Stop()
{
	for (auto& Pair : SceneActorMap)
	{
		Leave(Pair.Value);
	}
}

void UContextualAnimSceneInstance::OnMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted)
{
	UE_LOG(LogContextualAnim, Log, TEXT("UContextualAnimSceneInstance::OnMontageBlendingOut Montage: %s"), *GetNameSafe(Montage));

	for (auto& Pair : SceneActorMap)
	{
		if (Pair.Value.AnimDataPtr->Animation == Montage)
		{
			AActor* Actor = Pair.Value.GetActor();
			if (UAnimInstance* AnimInstance = TryGetAnimInstance(Actor))
			{
				AnimInstance->OnPlayMontageNotifyBegin.RemoveDynamic(this, &UContextualAnimSceneInstance::OnNotifyBeginReceived);
				AnimInstance->OnPlayMontageNotifyEnd.RemoveDynamic(this, &UContextualAnimSceneInstance::OnNotifyEndReceived);
				AnimInstance->OnMontageBlendingOut.RemoveDynamic(this, &UContextualAnimSceneInstance::OnMontageBlendingOut);

				if (UCharacterMovementComponent* CharacterMovementComp = Actor->FindComponentByClass<UCharacterMovementComponent>())
				{
					CharacterMovementComp->SetMovementMode(MOVE_Walking);
				}
			}

			SetIgnoreCollisionWithOtherActors(Pair.Value.GetActor(), false);

			OnActorLeft.ExecuteIfBound(this, Actor);

			break;
		}
	}

	bool bShouldEnd = true;
	for (auto& Pair : SceneActorMap)
	{
		const FContextualAnimSceneActorData& Data = Pair.Value;
		if (UAnimInstance* AnimInstance = TryGetAnimInstance(Data.GetActor()))
		{
			if(AnimInstance->Montage_IsPlaying(Data.AnimDataPtr->Animation))
			{
				bShouldEnd = false;
				break;
			}
		}
	}

	if(bShouldEnd)
	{
		OnSceneEnded.ExecuteIfBound(this);
	}
}

void UContextualAnimSceneInstance::OnNotifyBeginReceived(FName NotifyName, const FBranchingPointNotifyPayload& BranchingPointNotifyPayload)
{
	UE_LOG(LogContextualAnim, Log, TEXT("UContextualAnimSceneInstance::OnNotifyBeginReceived NotifyName: %s Montage: %s"),
	*NotifyName.ToString(), *GetNameSafe(BranchingPointNotifyPayload.SequenceAsset));

	// @TODO: For now just use a hard-coded name to identify the event. We should change this in the future
	static const FName LateJoinNotifyName = FName(TEXT("ContextualAnimLateJoin"));
	if(NotifyName == LateJoinNotifyName)
	{
		for (auto& Pair : SceneActorMap)
		{
			const FName& Role = Pair.Key;
			FContextualAnimSceneActorData& Data = Pair.Value;

			if (SceneAsset->GetJoinRuleForRole(Role) == EContextualAnimJoinRule::Late)
			{
				Join(Data);
			}
		}
	}
}

void UContextualAnimSceneInstance::OnNotifyEndReceived(FName NotifyName, const FBranchingPointNotifyPayload& BranchingPointNotifyPayload)
{
	UE_LOG(LogContextualAnim, Log, TEXT("UContextualAnimSceneInstance::OnNotifyEndReceived NotifyName: %s Montage: %s"),
	*NotifyName.ToString(), *GetNameSafe(BranchingPointNotifyPayload.SequenceAsset));
}

void UContextualAnimSceneInstance::SetIgnoreCollisionWithOtherActors(AActor* Actor, bool bValue) const
{
	for (auto& Pair : SceneActorMap)
	{
		AActor* OtherActor = Pair.Value.GetActor();
		if(OtherActor != Actor)
		{
			if (UPrimitiveComponent* RootPrimitiveComponent = Cast<UPrimitiveComponent>(Actor->GetRootComponent()))
			{
				RootPrimitiveComponent->IgnoreActorWhenMoving(OtherActor, bValue);
			}
		}
	}
}