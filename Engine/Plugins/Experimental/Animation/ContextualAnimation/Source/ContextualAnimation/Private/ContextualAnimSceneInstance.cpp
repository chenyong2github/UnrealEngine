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
#include "ContextualAnimTransition.h"
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
	if (const UAnimMontage* Animation = AnimDataPtr->Animation)
	{
		if (UAnimInstance* AnimInstance = GetAnimInstance())
		{
			return AnimInstance->GetActiveInstanceForMontage(Animation);
		}
	}

	return nullptr;
}

const UAnimMontage* FContextualAnimSceneActorData::GetAnimMontage() const
{
	const FAnimMontageInstance* MontageInstance = GetAnimMontageInstance();
	return MontageInstance ? MontageInstance->Montage : nullptr;
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

//================================================================================================================

void UContextualAnimSceneInstance::BreakContextualAnimSceneActorData(const FContextualAnimSceneActorData& SceneActorData, AActor*& Actor, UAnimMontage*& Montage, float& AnimTime, int32& CurrentSectionIndex, FName& CurrentSectionName)
{
	Actor = SceneActorData.GetActor();
	Montage = const_cast<UAnimMontage*>(SceneActorData.GetAnimMontage());
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
	UpdateTransitions(DeltaTime);
}

float UContextualAnimSceneInstance::GetResumePositionForSceneActor_Implementation(const FContextualAnimSceneActorData& SceneActorData, int32 DesiredSectionIndex) const
{
	// By default we resume from the same position in the section the leader is (we expect sections to have same length)

	float StartTime = 0.f;
	float EndTime = 0.f;
	SceneActorData.GetAnimMontage()->GetSectionStartAndEndTime(DesiredSectionIndex, StartTime, EndTime);

	const float TimeMaster = GetPositionInCurrentSection();
	const float ResumePosition = StartTime + TimeMaster;

	UE_LOG(LogContextualAnim, Verbose, TEXT("UContextualAnimSceneInstance::GetResumePositionForSceneActor Anim: %s DesiredSectionIndex: %d [%f %f] TimeMaster: %f ResumePosition: %f"),
		*GetNameSafe(SceneActorData.GetAnimMontage()), DesiredSectionIndex, StartTime, EndTime, TimeMaster, ResumePosition);

	return ResumePosition;
}

void UContextualAnimSceneInstance::UpdateTransitions(float DeltaTime)
{
	if (SceneAsset && SceneAsset->Transitions.Num() > 0)
	{
		FName CurrentSection = NAME_None;

		if (const FContextualAnimSceneActorData* LeaderSceneActorData = FindSceneActorDataForRole(SceneAsset->GetLeaderRole()))
		{
			const FAnimMontageInstance* LeaderMontageInstance = LeaderSceneActorData->GetAnimMontageInstance();
			if(LeaderMontageInstance)
			{
				CurrentSection = LeaderMontageInstance->GetCurrentSection();

				// Attempt to resume montages that has been paused due a failed transition
				for (auto& Pair : SceneActorMap)
				{
					FAnimMontageInstance* MontageInstance = Pair.Value.GetAnimMontageInstance();
					if (MontageInstance && !MontageInstance->IsPlaying())
					{
						const int32 DesiredSectionIndex = MontageInstance->Montage->GetSectionIndex(CurrentSection);
						if (DesiredSectionIndex != INDEX_NONE)
						{
							const float Position = GetResumePositionForSceneActor(Pair.Value, DesiredSectionIndex);
							MontageInstance->SetPosition(Position);
							MontageInstance->SetPlaying(true);
						}
					}
				}

				// Attempt to transition from current section
				for (const FContextualAnimTransitionContainer& TransitionData : SceneAsset->Transitions)
				{
					if (TransitionData.FromSections.Contains(CurrentSection))
					{
						//@TODO: bForceTransition should not be in the SceneAsset
						const bool bCanEnterTransition = TransitionData.bForceTransition || (TransitionData.Transition && TransitionData.Transition->CanEnterTransition(this, CurrentSection, TransitionData.ToSection));
						if (bCanEnterTransition)
						{
							for (auto& Pair : SceneActorMap)
							{
								const bool bTransitionToResult = TransitionTo(Pair.Value, TransitionData.ToSection);

								// If transition failed but we have a valid MontageInstance is usually because this montage doesn't have the desired section 
								// In that case, just pause the montage. We will resume as soon as we have a valid section (see above)
								if (bTransitionToResult == false)
								{
									if (FAnimMontageInstance* AnimMontageInstance = Pair.Value.GetAnimMontageInstance())
									{
										AnimMontageInstance->Pause();
									}
								}
							}

							// Break after find the first valid transition
							break;
						}
					}
				}
			}
		}
	}
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

const FContextualAnimSceneActorData* UContextualAnimSceneInstance::FindSceneActorDataForActor(const AActor* Actor) const
{
	if (Actor)
	{
		for (const auto& Pair : SceneActorMap)
		{
			const FContextualAnimSceneActorData& Data = Pair.Value;
			if (Data.GetActor() == Actor)
			{
				return &Data;
			}
		}
	}

	return nullptr;
}

const FContextualAnimSceneActorData* UContextualAnimSceneInstance::FindSceneActorDataForRole(const FName& Role) const
{
	return SceneActorMap.Find(Role);
}

AActor* UContextualAnimSceneInstance::GetActorByRole(FName Role) const
{
	const FContextualAnimSceneActorData* SceneActorData = FindSceneActorDataForRole(Role);
	return SceneActorData ? SceneActorData->GetActor() : nullptr;
}

void UContextualAnimSceneInstance::Join(FContextualAnimSceneActorData& SceneActorData)
{
	AActor* Actor = SceneActorData.GetActor();
	if (UAnimInstance* AnimInstance = SceneActorData.GetAnimInstance())
	{
		if (UMotionWarpingComponent* MotionWarpingComp = Actor->FindComponentByClass<UMotionWarpingComponent>())
		{
			for (const auto& Pair : AlignmentSectionToScenePivotList)
			{
				const FName SyncPointName = Pair.Key;
				const float SyncTime = SceneActorData.GetAnimData()->GetSyncTimeForWarpSection(SyncPointName);
				const FTransform AlignmentTransform = (SceneActorData.GetAnimData()->AlignmentData.ExtractTransformAtTime(SyncPointName, SyncTime) * Pair.Value);
				MotionWarpingComp->AddOrUpdateWarpTargetFromTransform(SyncPointName, AlignmentTransform);
			}
		}

		UAnimMontage* Animation = SceneActorData.GetAnimData()->Animation;
		AnimInstance->Montage_Play(Animation, 1.f, EMontagePlayReturnType::MontageLength, SceneActorData.GetAnimStartTime());

		AnimInstance->OnPlayMontageNotifyBegin.AddUniqueDynamic(this, &UContextualAnimSceneInstance::OnNotifyBeginReceived);
		AnimInstance->OnPlayMontageNotifyEnd.AddUniqueDynamic(this, &UContextualAnimSceneInstance::OnNotifyEndReceived);
		AnimInstance->OnMontageBlendingOut.AddUniqueDynamic(this, &UContextualAnimSceneInstance::OnMontageBlendingOut);

		if(SceneActorData.GetAnimData()->bRequireFlyingMode)
		{
			if (UCharacterMovementComponent* CharacterMovementComp = Actor->FindComponentByClass<UCharacterMovementComponent>())
			{
				CharacterMovementComp->SetMovementMode(MOVE_Flying);
			}
		}
	}

	if(SceneAsset->bDisableCollisionBetweenActors)
	{
		SetIgnoreCollisionWithOtherActors(Actor, true);
	}

	SceneActorData.SceneInstancePtr = this;

	if(UContextualAnimSceneActorComponent* SceneActorComp = SceneActorData.GetSceneActorComponent())
	{
		SceneActorComp->OnJoinedScene(&SceneActorData);
	}

	OnActorJoined.ExecuteIfBound(this, Actor);
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
			if (ensureAlways(CurrentMontage))
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

		//DrawDebugCoordinateSystem(GetWorld(), ScenePivot.GetLocation(), ScenePivot.Rotator(), 100.f, false, 10.f, 0, 5.f);
	}

	for (auto& Pair : SceneActorMap)
	{
		const FName& Role = Pair.Key;
		FContextualAnimSceneActorData& Data = Pair.Value;

		if (SceneAsset->GetTrackSettings(Role)->JoinRule == EContextualAnimJoinRule::Default)
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
		FContextualAnimSceneActorData& SceneActorData = Pair.Value;
		if (SceneActorData.GetAnimData()->Animation == Montage)
		{
			AActor* Actor = SceneActorData.GetActor();
			if (UAnimInstance* AnimInstance = SceneActorData.GetAnimInstance())
			{
				AnimInstance->OnPlayMontageNotifyBegin.RemoveDynamic(this, &UContextualAnimSceneInstance::OnNotifyBeginReceived);
				AnimInstance->OnPlayMontageNotifyEnd.RemoveDynamic(this, &UContextualAnimSceneInstance::OnNotifyEndReceived);
				AnimInstance->OnMontageBlendingOut.RemoveDynamic(this, &UContextualAnimSceneInstance::OnMontageBlendingOut);

				if (SceneActorData.GetAnimData()->bRequireFlyingMode)
				{
					if (UCharacterMovementComponent* CharacterMovementComp = Actor->FindComponentByClass<UCharacterMovementComponent>())
					{
						CharacterMovementComp->SetMovementMode(MOVE_Walking);
					}
				}
			}

			if (SceneAsset->bDisableCollisionBetweenActors)
			{
				SetIgnoreCollisionWithOtherActors(SceneActorData.GetActor(), false);
			}

			if (UContextualAnimSceneActorComponent* SceneActorComp = Pair.Value.GetSceneActorComponent())
			{
				SceneActorComp->OnLeftScene(&SceneActorData);
			}

			OnActorLeft.ExecuteIfBound(this, Actor);

			break;
		}
	}

	bool bShouldEnd = true;
	for (auto& Pair : SceneActorMap)
	{
		const FContextualAnimSceneActorData& Data = Pair.Value;
		if (UAnimInstance* AnimInstance = Data.GetAnimInstance())
		{
			if(AnimInstance->Montage_IsPlaying(Data.GetAnimData()->Animation))
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

			if (SceneAsset->GetTrackSettings(Role)->JoinRule == EContextualAnimJoinRule::Late)
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

float UContextualAnimSceneInstance::GetCurrentSectionTimeLeft() const
{
	if(SceneAsset)
	{
		if(const FContextualAnimSceneActorData* Data = FindSceneActorDataForRole(SceneAsset->GetLeaderRole()))
		{
			if (const FAnimMontageInstance* MontageInstance = Data->GetAnimMontageInstance())
			{
				return MontageInstance->Montage->GetSectionTimeLeftFromPos(MontageInstance->GetPosition());
			}
		}
	}
	
	return 0.f;
}

float UContextualAnimSceneInstance::GetPositionInCurrentSection() const
{
	if (SceneAsset)
	{
		if (const FContextualAnimSceneActorData* Data = FindSceneActorDataForRole(SceneAsset->GetLeaderRole()))
		{
			if (const FAnimMontageInstance* MontageInstance = Data->GetAnimMontageInstance())
			{
				float PosInSection = 0.f;
				MontageInstance->Montage->GetAnimCompositeSectionIndexFromPos(MontageInstance->GetPosition(), PosInSection);
				return PosInSection;
			}
		}
	}

	return 0.f;
}

bool UContextualAnimSceneInstance::DidCurrentSectionLoop() const
{
	if (SceneAsset)
	{
		if (const FContextualAnimSceneActorData* Data = FindSceneActorDataForRole(SceneAsset->GetLeaderRole()))
		{
			if (const FAnimMontageInstance* MontageInstance = Data->GetAnimMontageInstance())
			{
				const float PreviousPos = MontageInstance->GetPreviousPosition();
				const float CurrentPos = MontageInstance->GetPosition();

				const int32 SectionIdxPreviousPos = MontageInstance->Montage->GetSectionIndexFromPosition(PreviousPos);
				const int32 SectionIdxCurrentPos = MontageInstance->Montage->GetSectionIndexFromPosition(CurrentPos);

				if(SectionIdxPreviousPos == SectionIdxCurrentPos)
				{
					const float TimeLeftFromPreviousPos = MontageInstance->Montage->GetSectionTimeLeftFromPos(PreviousPos);
					const float TimeLeftFromCurrentPos = MontageInstance->Montage->GetSectionTimeLeftFromPos(CurrentPos);

					return TimeLeftFromPreviousPos < TimeLeftFromCurrentPos;
				}
			}
		}
	}

	return false;
}
