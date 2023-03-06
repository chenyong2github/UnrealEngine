// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimSceneActorComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ContextualAnimSelectionCriterion.h"
#include "Components/SkeletalMeshComponent.h"
#include "ContextualAnimManager.h"
#include "ContextualAnimSceneAsset.h"
#include "ContextualAnimUtilities.h"
#include "AnimNotifyState_IKWindow.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "IKRigDataTypes.h"
#include "Net/Core/PushModel/PushModel.h"
#include "Physics/PhysicsInterfaceTypes.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveViewRelevance.h"
#include "SceneManagement.h"
#include "MotionWarpingComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ContextualAnimSceneActorComponent)

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
TAutoConsoleVariable<int32> CVarContextualAnimIKDebug(TEXT("a.ContextualAnim.IK.Debug"), 0, TEXT("Draw Debug IK Targets"));
TAutoConsoleVariable<float> CVarContextualAnimIKDrawDebugLifetime(TEXT("a.ContextualAnim.IK.DrawDebugLifetime"), 0, TEXT("Draw Debug Duration"));
#endif

void FContextualAnimRepData::IncrementRepCounter()
{
	static uint8 Counter = 0;
	Counter = Counter < UINT8_MAX ? Counter + 1 : 0;
	RepCounter = Counter;
}

UContextualAnimSceneActorComponent::UContextualAnimSceneActorComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(true);

	SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void UContextualAnimSceneActorComponent::GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(UContextualAnimSceneActorComponent, RepBindings, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UContextualAnimSceneActorComponent, RepLateJoinData, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UContextualAnimSceneActorComponent, RepTransitionSingleActorData, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UContextualAnimSceneActorComponent, RepTransitionData, Params);
}

bool UContextualAnimSceneActorComponent::IsOwnerLocallyControlled() const
{
	if (const APawn* OwnerPawn = Cast<APawn>(GetOwner()))
	{
		return OwnerPawn->IsLocallyControlled();
	}

	return false;
}

void UContextualAnimSceneActorComponent::PlayAnimation_Internal(UAnimSequenceBase* Animation, float StartTime, bool bSyncPlaybackTime)
{
	TGuardValue<bool> UpdateGuard(bGuardAnimEvents, true);

	if (UAnimInstance* AnimInstance = UContextualAnimUtilities::TryGetAnimInstance(GetOwner()))
	{
		UE_LOG(LogContextualAnim, VeryVerbose, TEXT("%-21s \t\tUContextualAnimSceneActorComponent::PlayAnimation_Internal Playing Animation. Actor: %s Anim: %s StartTime: %f bSyncPlaybackTime: %d"),
			*UEnum::GetValueAsString(TEXT("Engine.ENetRole"), GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()), *GetNameSafe(Animation), StartTime, bSyncPlaybackTime);

		//@TODO: Add support for dynamic montage
		UAnimMontage* AnimMontage = Cast<UAnimMontage>(Animation);
		AnimInstance->Montage_Play(AnimMontage, 1.f, EMontagePlayReturnType::MontageLength, StartTime);

		AnimInstance->OnMontageBlendingOut.AddUniqueDynamic(this, &UContextualAnimSceneActorComponent::OnMontageBlendingOut);

		if (bSyncPlaybackTime)
		{
			if (FAnimMontageInstance* MontageInstance = AnimInstance->GetActiveMontageInstance())
			{
				if (const FContextualAnimSceneBinding* SyncLeader = Bindings.GetSyncLeader())
				{
					if (SyncLeader->GetActor() != GetOwner())
					{
						FAnimMontageInstance* LeaderMontageInstance = SyncLeader->GetAnimMontageInstance();
						if (LeaderMontageInstance && LeaderMontageInstance->Montage == Bindings.GetAnimTrackFromBinding(*SyncLeader).Animation && MontageInstance->GetMontageSyncLeader() == nullptr)
						{
							UE_LOG(LogContextualAnim, VeryVerbose, TEXT("%-21s \t\tUContextualAnimSceneActorComponent::PlayAnimation_Internal Syncing Animation. Actor: %s Anim: %s StartTime: %f bSyncPlaybackTime: %d"),
								*UEnum::GetValueAsString(TEXT("Engine.ENetRole"), GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()), *GetNameSafe(Animation), StartTime, bSyncPlaybackTime);

							MontageInstance->MontageSync_Follow(LeaderMontageInstance);
						}
					}
				}
			}
		}
	}

	USkeletalMeshComponent* SkelMeshComp = UContextualAnimUtilities::TryGetSkeletalMeshComponent(GetOwner());
	if (SkelMeshComp && !SkelMeshComp->OnTickPose.IsBoundToObject(this))
	{
		SkelMeshComp->OnTickPose.AddUObject(this, &UContextualAnimSceneActorComponent::OnTickPose);
	}
}

void UContextualAnimSceneActorComponent::AddOrUpdateWarpTargets(int32 SectionIdx, int32 AnimSetIdx)
{
	// This is relevant only for character with motion warping comp
	ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner());
	UMotionWarpingComponent* MotionWarpComp = CharacterOwner ? CharacterOwner->GetComponentByClass<UMotionWarpingComponent>() : nullptr;
	if (MotionWarpComp == nullptr)
	{
		return;
	}

	if (const FContextualAnimSceneBinding* Binding = Bindings.FindBindingByActor(GetOwner()))
	{
		const UContextualAnimSceneAsset* Asset = Bindings.GetSceneAsset();
		check(Asset);

		const FContextualAnimSceneSection* Section = Asset->GetSection(SectionIdx);
		check(Section);

		if(Section->GetWarpPointDefinitions().Num() > 0)
		{
			const FContextualAnimTrack* AnimTrack = Asset->GetAnimTrack(SectionIdx, AnimSetIdx, Bindings.GetRoleFromBinding(*Binding));
			if (AnimTrack == nullptr || AnimTrack->Animation == nullptr)
			{
				return;
			}

			for (const FContextualAnimWarpPointDefinition& WarpPointDef : Section->GetWarpPointDefinitions())
			{
				FContextualAnimWarpPoint WarpPoint;
				if (Bindings.CalculateWarpPoint(WarpPointDef, WarpPoint))
				{
					const float Time = AnimTrack->GetSyncTimeForWarpSection(WarpPointDef.WarpTargetName);
					const FTransform TransformRelativeToWarpPoint = Asset->GetAlignmentTransform(*AnimTrack, WarpPointDef.WarpTargetName, Time);
					const FTransform WarpTargetTransform = TransformRelativeToWarpPoint * WarpPoint.Transform;
					MotionWarpComp->AddOrUpdateWarpTargetFromTransform(WarpPoint.Name, WarpTargetTransform);
				}
			}
		}

		if (Binding->HasExternalWarpTarget())
		{
			MotionWarpComp->AddOrUpdateWarpTargetFromTransform(Binding->GetExternalWarpTargetName(), Binding->GetExternalWarpTargetTransform());
		}
	}
}

bool UContextualAnimSceneActorComponent::LateJoinContextualAnimScene(AActor* Actor, FName Role, const TArray<FContextualAnimWarpTarget>& WarpTargets)
{
	if (!Bindings.IsValid())
	{
		UE_LOG(LogContextualAnim, Warning, TEXT("%-21s UContextualAnimSceneActorComponent::LateJoinContextualAnimScene Invalid Bindings"), *UEnum::GetValueAsString(TEXT("Engine.ENetRole"), GetOwner()->GetLocalRole()));
		return false;
	}

	// Redirect the request to the leader if needed. Technically this is not necessary but the idea here is that the leader of the interaction handles all the events for that interaction
	// E.g the leader tells other actors to play the animation.
	if (const FContextualAnimSceneBinding* Leader = Bindings.GetSyncLeader())
	{
		if (Leader->GetActor() != GetOwner())
		{
			if (UContextualAnimSceneActorComponent* Comp = Leader->GetSceneActorComponent())
			{
				return Comp->LateJoinContextualAnimScene(Actor, Role, WarpTargets);
			}
		}
	}

	UE_LOG(LogContextualAnim, Verbose, TEXT("%-21s UContextualAnimSceneActorComponent::LateJoinContextualAnimScene Owner: %s Bindings Id: %d Section: %d Asset: %s. Requester: %s Role: %s"),
		*UEnum::GetValueAsString(TEXT("Engine.ENetRole"), GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()), Bindings.GetID(), Bindings.GetSectionIdx(), *GetNameSafe(Bindings.GetSceneAsset()), *GetNameSafe(Actor), *Role.ToString());

	// Play animation and set state on this new actor that is joining us and update bindings for everyone else
	if (HandleLateJoin(Actor, Role, WarpTargets))
	{
		// Replicate late join event. See OnRep_LateJoinData
		if (GetOwner()->HasAuthority())
		{
			RepLateJoinData.Actor = Actor;
			RepLateJoinData.Role = Role;
			RepLateJoinData.IncrementRepCounter();
			MARK_PROPERTY_DIRTY_FROM_NAME(UContextualAnimSceneActorComponent, RepLateJoinData, this);
			GetOwner()->ForceNetUpdate();
		}

		return true;
	}

	return false;
}

bool UContextualAnimSceneActorComponent::HandleLateJoin(AActor* Actor, FName Role, const TArray<FContextualAnimWarpTarget>& WarpTargets)
{
	if (!Bindings.BindActorToRole(*Actor, Role))
	{
		UE_LOG(LogContextualAnim, Warning, TEXT("%-21s UContextualAnimSceneActorComponent::HandleLateJoin Failed. Reason: Adding %s to the bindings for role: %s failed!"),
			*UEnum::GetValueAsString(TEXT("Engine.ENetRole"), GetOwner()->GetLocalRole()), *GetNameSafe(Actor), *Role.ToString());

		return false;
	}

	// Update the bindings on all the other actors too
	for (const FContextualAnimSceneBinding& OtherBinding : Bindings)
	{
		if (OtherBinding.GetActor() != GetOwner() && OtherBinding.GetActor() != Actor)
		{
			if (UContextualAnimSceneActorComponent* Comp = OtherBinding.GetSceneActorComponent())
			{
				Comp->Bindings.BindActorToRole(*Actor, Role);
			}
		}
	}

	// Play animation and set state on this new actor that is joining us
	if (const FContextualAnimSceneBinding* Binding = Bindings.FindBindingByActor(Actor))
	{
		if (UContextualAnimSceneActorComponent* Comp = Binding->GetSceneActorComponent())
		{
			Comp->LateJoinScene(Bindings, WarpTargets);
		}
	}

	return true;
}

void UContextualAnimSceneActorComponent::LateJoinScene(const FContextualAnimSceneBindings& InBindings, const TArray<FContextualAnimWarpTarget>& WarpTargets)
{
	if (Bindings.IsValid())
	{
		UE_LOG(LogContextualAnim, Verbose, TEXT("%-21s UContextualAnimSceneActorComponent::LateJoinScene Actor: %s Bindings Id: %d Section: %d Asset: %s. Leaving current scene"),
			*UEnum::GetValueAsString(TEXT("Engine.ENetRole"), GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()), Bindings.GetID(), Bindings.GetSectionIdx(), *GetNameSafe(Bindings.GetSceneAsset()));

		LeaveScene();
	}

	if (const FContextualAnimSceneBinding* Binding = InBindings.FindBindingByActor(GetOwner()))
	{
		UE_LOG(LogContextualAnim, Verbose, TEXT("%-21s UContextualAnimSceneActorComponent::LateJoinScene Actor: %s Role: %s Bindings Id: %d Section: %d Asset: %s"),
			*UEnum::GetValueAsString(TEXT("Engine.ENetRole"), GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()), *InBindings.GetRoleFromBinding(*Binding).ToString(), InBindings.GetID(), InBindings.GetSectionIdx(), *GetNameSafe(InBindings.GetSceneAsset()));

		Bindings = InBindings;

		for (const FContextualAnimWarpTarget& WarpTarget : WarpTargets)
		{
			Bindings.SetRoleWarpTarget(WarpTarget.Role, WarpTarget.TargetName, WarpTarget.TargetTransform);
		}

		// For now when late joining an scene always play animation from first section
		const int32 SectionIdx = 0;
		const int32 AnimSetIdx = 0;
		const FContextualAnimTrack* AnimTrack = Bindings.GetSceneAsset()->GetAnimTrack(SectionIdx, AnimSetIdx, Bindings.GetRoleFromBinding(*Binding));
		check(AnimTrack);

		PlayAnimation_Internal(AnimTrack->Animation, 0.f, false);

		AddOrUpdateWarpTargets(SectionIdx, AnimSetIdx);

		SetIgnoreCollisionWithOtherActors(true);

		SetMovementState(AnimTrack->bRequireFlyingMode);
	}
}

void UContextualAnimSceneActorComponent::OnRep_LateJoinData()
{
	// This is received by the leader of the interaction on every remote client

	if (!Bindings.IsValid())
	{
		UE_LOG(LogContextualAnim, Warning, TEXT("%-21s UContextualAnimSceneActorComponent::OnRep_LateJoinData Invalid Bindings"), *UEnum::GetValueAsString(TEXT("Engine.ENetRole"), GetOwner()->GetLocalRole()));
		return;
	}

	UE_LOG(LogContextualAnim, Verbose, TEXT("%-21s UContextualAnimSceneActorComponent::OnRep_LateJoinData Owner: %s Bindings Id: %d Section: %d Asset: %s. Requester: %s Role: %s"),
		*UEnum::GetValueAsString(TEXT("Engine.ENetRole"), GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()), Bindings.GetID(), Bindings.GetSectionIdx(), *GetNameSafe(Bindings.GetSceneAsset()), *GetNameSafe(RepLateJoinData.Actor), *RepLateJoinData.Role.ToString());

	// Play animation and set state on this new actor that is joining us and update bindings for everyone else
	HandleLateJoin(RepLateJoinData.Actor, RepLateJoinData.Role, {});
}

bool UContextualAnimSceneActorComponent::TransitionContextualAnimScene(FName SectionName, const TArray<FContextualAnimWarpTarget>& WarpTargets)
{
	if (!GetOwner()->HasAuthority())
	{
		return false;
	}

	// Redirect the request to the leader if needed. Technically this is not necessary but the idea here is that the leader of the interaction handles all the events for that interaction
	// E.g the leader tells other actors to play the animation.
	if (const FContextualAnimSceneBinding* Leader = Bindings.GetSyncLeader())
	{
		if (Leader->GetActor() != GetOwner())
		{
			if (UContextualAnimSceneActorComponent* Comp = Leader->GetSceneActorComponent())
			{
				return Comp->TransitionContextualAnimScene(SectionName, WarpTargets);
			}
		}
	}

	if (const FContextualAnimSceneBinding* OwnerBinding = Bindings.FindBindingByActor(GetOwner()))
	{
		for (const FContextualAnimWarpTarget& WarpTarget : WarpTargets)
		{
			Bindings.SetRoleWarpTarget(WarpTarget.Role, WarpTarget.TargetName, WarpTarget.TargetTransform);
		}

		const int32 SectionIdx = Bindings.GetSceneAsset()->GetSectionIndex(SectionName);
		if (SectionIdx != INDEX_NONE)
		{
			UE_LOG(LogContextualAnim, Log, TEXT("%-21s UContextualAnimSceneActorComponent::TransitionTo Actor: %s SectionName: %s"),
				*UEnum::GetValueAsString(TEXT("Engine.ENetRole"), GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()), *SectionName.ToString());

			HandleTransitionEveryone(SectionIdx, 0);

			RepTransitionData.SectionIdx = SectionIdx;
			RepTransitionData.AnimSetIdx = 0;
			RepTransitionData.IncrementRepCounter();
			MARK_PROPERTY_DIRTY_FROM_NAME(UContextualAnimSceneActorComponent, RepTransitionData, this);
			GetOwner()->ForceNetUpdate();

			return true;
		}
	}

	return false;
}

void UContextualAnimSceneActorComponent::HandleTransitionEveryone(int32 NewSectionIdx, int32 NewAnimSetIdx)
{
	// Update Bindings internal data and play new animation for the leader first
	// Note that for now we always transition to the first set in the section. We could run selection criteria here too but keeping it simple for now
	HandleTransitionSelf(NewSectionIdx, NewAnimSetIdx);

	// And now the same for everyone else
	for (const FContextualAnimSceneBinding& Binding : Bindings)
	{
		if (Binding.GetActor() != GetOwner())
		{
			if(UContextualAnimSceneActorComponent* Comp = Binding.GetSceneActorComponent())
			{
				Comp->HandleTransitionSelf(NewSectionIdx, NewAnimSetIdx);
			}
		}
	}
}

void UContextualAnimSceneActorComponent::HandleTransitionSelf(int32 NewSectionIdx, int32 NewAnimSetIdx)
{
	// Update bindings internal data so it points to the new section and new anim set
	Bindings.TransitionTo(NewSectionIdx, NewAnimSetIdx);

	// Play animation
	//@TODO: Add support for dynamic montage
	const FContextualAnimTrack& AnimTrack = Bindings.GetAnimTrackFromBinding(*Bindings.FindBindingByActor(GetOwner()));
	PlayAnimation_Internal(AnimTrack.Animation, 0.f, true);

	AddOrUpdateWarpTargets(NewSectionIdx, NewAnimSetIdx);
}

bool UContextualAnimSceneActorComponent::TransitionSingleActor(int32 SectionIdx, int32 AnimSetIdx, const TArray<FContextualAnimWarpTarget>& WarpTargets)
{
	if (!GetOwner()->HasAuthority())
	{
		return false;
	}

	if (const FContextualAnimSceneBinding* OwnerBinding = Bindings.FindBindingByActor(GetOwner()))
	{
		for (const FContextualAnimWarpTarget& WarpTarget : WarpTargets)
		{
			Bindings.SetRoleWarpTarget(WarpTarget.Role, WarpTarget.TargetName, WarpTarget.TargetTransform);
		}

		if (const UContextualAnimSceneAsset* Asset = Bindings.GetSceneAsset())
		{
			const FContextualAnimTrack* AnimTrack = Asset->GetAnimTrack(SectionIdx, AnimSetIdx, Bindings.GetRoleFromBinding(*OwnerBinding));
			if (AnimTrack && AnimTrack->Animation)
			{
				UE_LOG(LogContextualAnim, Log, TEXT("%-21s UContextualAnimSceneActorComponent::TransitionSingleActor Actor: %s SectionIdx: %d AnimSetIdx: %d"),
					*UEnum::GetValueAsString(TEXT("Engine.ENetRole"), GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()), SectionIdx, AnimSetIdx);

				PlayAnimation_Internal(AnimTrack->Animation, 0.f, false);

				AddOrUpdateWarpTargets(SectionIdx, AnimSetIdx);

				RepTransitionSingleActorData.SectionIdx = SectionIdx;
				RepTransitionSingleActorData.AnimSetIdx = AnimSetIdx;
				RepTransitionSingleActorData.IncrementRepCounter();
				MARK_PROPERTY_DIRTY_FROM_NAME(UContextualAnimSceneActorComponent, RepTransitionSingleActorData, this);
				GetOwner()->ForceNetUpdate();

				return true;
			}
		}
	}

	return false;
}

void UContextualAnimSceneActorComponent::OnRep_RepTransitionSingleActor()
{
	UE_LOG(LogContextualAnim, Verbose, TEXT("%-21s UContextualAnimSceneActorComponent::OnRep_RepTransitionSingleActor Owner: %s SectionIdx: %s AnimSetIdx: %d"),
		*UEnum::GetValueAsString(TEXT("Engine.ENetRole"), GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()), RepTransitionSingleActorData.SectionIdx, RepTransitionSingleActorData.AnimSetIdx);

	if (const FContextualAnimSceneBinding* OwnerBinding = Bindings.FindBindingByActor(GetOwner()))
	{
		if (const UContextualAnimSceneAsset* Asset = Bindings.GetSceneAsset())
		{
			const FContextualAnimTrack* AnimTrack = Asset->GetAnimTrack(RepTransitionSingleActorData.SectionIdx, RepTransitionSingleActorData.AnimSetIdx, Bindings.GetRoleFromBinding(*OwnerBinding));
			if (AnimTrack && AnimTrack->Animation)
			{
				PlayAnimation_Internal(AnimTrack->Animation, 0.f, false);
				AddOrUpdateWarpTargets(RepTransitionSingleActorData.SectionIdx, RepTransitionSingleActorData.AnimSetIdx);
			}
		}
	}
}

bool UContextualAnimSceneActorComponent::StartContextualAnimScene(const FContextualAnimSceneBindings& InBindings)
{
	return StartContextualAnimScene(InBindings, {});
}
	
bool UContextualAnimSceneActorComponent::LateJoinContextualAnimScene(AActor* Actor, FName Role)
{
	return LateJoinContextualAnimScene(Actor, Role, {});
}
	
bool UContextualAnimSceneActorComponent::TransitionContextualAnimScene(FName SectionName)
{
	return TransitionContextualAnimScene(SectionName, {});
}
	
bool UContextualAnimSceneActorComponent::TransitionSingleActor(int32 SectionIdx, int32 AnimSetIdx)
{
	return TransitionSingleActor(SectionIdx, AnimSetIdx, {});
}

bool UContextualAnimSceneActorComponent::StartContextualAnimScene(const FContextualAnimSceneBindings& InBindings, const TArray<FContextualAnimWarpTarget>& WarpTargets)
{
	UE_LOG(LogContextualAnim, Log, TEXT("%-21s UContextualAnimSceneActorComponent::StartContextualAnim Actor: %s"),
		*UEnum::GetValueAsString(TEXT("Engine.ENetRole"), GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()));

	const FContextualAnimSceneBinding* OwnerBinding = InBindings.FindBindingByActor(GetOwner());
	if (ensureAlways(OwnerBinding))
	{
		if (GetOwner()->HasAuthority())
		{
			JoinScene(InBindings, WarpTargets);

			for (const FContextualAnimSceneBinding& Binding : InBindings)
			{
				if (Binding.GetActor() != GetOwner())
				{
					if (UContextualAnimSceneActorComponent* Comp = Binding.GetSceneActorComponent())
					{
						Comp->JoinScene(InBindings, WarpTargets);
					}
				}
			}

			RepBindings = InBindings;
			MARK_PROPERTY_DIRTY_FROM_NAME(UContextualAnimSceneActorComponent, RepBindings, this);
			GetOwner()->ForceNetUpdate();

			return true;
		}
		else if (GetOwner()->GetLocalRole() == ROLE_AutonomousProxy)
		{
			JoinScene(InBindings, WarpTargets);

			ServerStartContextualAnimScene(InBindings);

			return true;
		}
	}

	return false;
}

void UContextualAnimSceneActorComponent::ServerStartContextualAnimScene_Implementation(const FContextualAnimSceneBindings& InBindings)
{
	StartContextualAnimScene(InBindings, {});
}

bool UContextualAnimSceneActorComponent::ServerStartContextualAnimScene_Validate(const FContextualAnimSceneBindings& InBindings)
{
	return true;
}

void UContextualAnimSceneActorComponent::EarlyOutContextualAnimScene()
{
	if (const FContextualAnimSceneBinding* Binding = Bindings.FindBindingByActor(GetOwner()))
	{
		const UAnimInstance* AnimInstance = Binding->GetAnimInstance();
		const UAnimMontage* ActiveMontage = AnimInstance ? AnimInstance->GetCurrentActiveMontage() : nullptr;
		if (ActiveMontage)
		{
			UE_LOG(LogContextualAnim, Verbose, TEXT("%-21s UContextualAnimSceneActorComponent::EarlyOutContextualAnimScene Actor: %s ActiveMontage: %s"),
				*UEnum::GetValueAsString(TEXT("Engine.ENetRole"), GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()), *GetNameSafe(ActiveMontage));

			if (Bindings.GetAnimTrackFromBinding(*Binding).Animation == ActiveMontage)
			{
				// Stop animation.
				LeaveScene();

				// If we are on the server, rep bindings to stop animation on simulated proxies
				if (GetOwner()->HasAuthority())
				{
					if (RepBindings.IsValid())
					{
						RepBindings.Clear();
						MARK_PROPERTY_DIRTY_FROM_NAME(UContextualAnimSceneActorComponent, RepBindings, this);

						GetOwner()->ForceNetUpdate();
					}
				}
				// If local player, tell the server to stop the animation too
				else if (GetOwner()->GetLocalRole() == ROLE_AutonomousProxy)
				{
					ServerEarlyOutContextualAnimScene();
				}
			}
		}
	}
}

void UContextualAnimSceneActorComponent::ServerEarlyOutContextualAnimScene_Implementation()
{
	EarlyOutContextualAnimScene();
}

bool UContextualAnimSceneActorComponent::ServerEarlyOutContextualAnimScene_Validate()
{
	return true;
}

void UContextualAnimSceneActorComponent::OnRep_TransitionData()
{
	UE_LOG(LogContextualAnim, Verbose, TEXT("%-21s UContextualAnimSceneActorComponent::OnRep_TransitionData Actor: %s"),
		*UEnum::GetValueAsString(TEXT("Engine.ENetRole"), GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()));

	HandleTransitionEveryone(RepTransitionData.SectionIdx, RepTransitionData.AnimSetIdx);
}

void UContextualAnimSceneActorComponent::OnRep_Bindings()
{
	UE_LOG(LogContextualAnim, Verbose, TEXT("%-21s UContextualAnimSceneActorComponent::OnRep_Bindings Actor: %s RepBindings Id: %d Num: %d Bindings Id: %d Num: %d"),
		*UEnum::GetValueAsString(TEXT("Engine.ENetRole"), GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()), RepBindings.GetID(), RepBindings.Num(), Bindings.GetID(), Bindings.Num());

	// The owner of this component started an interaction on the server
	if (RepBindings.IsValid())
	{
		const FContextualAnimSceneBinding* OwnerBinding = RepBindings.FindBindingByActor(GetOwner());
		if (ensureAlways(OwnerBinding))
		{
			// Join the scene (start playing animation, etc.)
			if (GetOwner()->GetLocalRole() != ROLE_AutonomousProxy)
			{
				JoinScene(RepBindings, {});
			}

			// RepBindings is only replicated from the initiator of the action.
			// So now we have to tell everyone else involved in the interaction to join us
			// @TODO: For now this assumes that all the actors will start playing the animation at the same time. 
			// We will expand this in the future to allow 'late' join
			for (const FContextualAnimSceneBinding& Binding : RepBindings)
			{
				if (Binding.GetActor() != GetOwner())
				{
					if (UContextualAnimSceneActorComponent* Comp = Binding.GetSceneActorComponent())
					{
						Comp->JoinScene(RepBindings, {});
					}
				}
			}
		}
	}
	else
	{	
		// Empty bindings is replicated by the initiator of the interaction when the animation ends
		// In this case we don't want to tell everyone else to also leave the scene since there is very common for the initiator, 
		// specially if is player character, to end the animation earlier for responsiveness
		// It is more likely this will do nothing since we listen to montage end also on Simulated Proxies to 'predict' the end of the interaction.
		if (RepBindings.GetID() == Bindings.GetID() && GetOwner()->GetLocalRole() != ROLE_AutonomousProxy)
		{
			LeaveScene();
		}
	}
}

FBoxSphereBounds UContextualAnimSceneActorComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	// The option of having an SceneAsset and draw options on this component may go away in the future anyway, replaced by smart objects.
	const float Radius = SceneAsset && SceneAsset->HasValidData() ? SceneAsset->GetRadius() : 0.f;
	return FBoxSphereBounds(FSphere(GetComponentTransform().GetLocation(), Radius));
}

void UContextualAnimSceneActorComponent::OnRegister()
{
	Super::OnRegister();

	UContextualAnimManager* ContextAnimManager = UContextualAnimManager::Get(GetWorld());
	if (ensure(!bRegistered) && ContextAnimManager)
	{
		ContextAnimManager->RegisterSceneActorComponent(this);
		bRegistered = true;
	}
}

void UContextualAnimSceneActorComponent::OnUnregister()
{
	Super::OnUnregister();

	UContextualAnimManager* ContextAnimManager = UContextualAnimManager::Get(GetWorld());
	if (bRegistered && ContextAnimManager)
	{
		ContextAnimManager->UnregisterSceneActorComponent(this);
		bRegistered = false;
	}
}

void UContextualAnimSceneActorComponent::SetIgnoreCollisionWithOtherActors(bool bValue) const
{
	const AActor* OwnerActor = GetOwner();

	for (const FContextualAnimSceneBinding& Binding : Bindings)
	{
		AActor* OtherActor = Binding.GetActor();
		if (OtherActor != OwnerActor)
		{
			if (UPrimitiveComponent* RootPrimitiveComponent = Cast<UPrimitiveComponent>(OwnerActor->GetRootComponent()))
			{
				RootPrimitiveComponent->IgnoreActorWhenMoving(OtherActor, bValue);
			}
		}
	}
}

void UContextualAnimSceneActorComponent::OnJoinedScene(const FContextualAnimSceneBindings& InBindings)
{
	UE_LOG(LogContextualAnim, Verbose, TEXT("%-21s UContextualAnimSceneActorComponent::OnJoinedScene Actor: %s InBindings Id: %d"),
		*UEnum::GetValueAsString(TEXT("Engine.ENetRole"), GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()), InBindings.GetID());

	if (Bindings.IsValid())
	{
		OnLeftScene();
	}

	if (const FContextualAnimSceneBinding* Binding = InBindings.FindBindingByActor(GetOwner()))
	{
		Bindings = InBindings;

		USkeletalMeshComponent* SkelMeshComp = UContextualAnimUtilities::TryGetSkeletalMeshComponent(GetOwner());
		if (SkelMeshComp && !SkelMeshComp->OnTickPose.IsBoundToObject(this))
		{
			SkelMeshComp->OnTickPose.AddUObject(this, &UContextualAnimSceneActorComponent::OnTickPose);
		}

		// Disable collision between actors so they can align perfectly
		SetIgnoreCollisionWithOtherActors(true);

		// Prevent physics rotation. During the interaction we want to be fully root motion driven
		if (UCharacterMovementComponent* MovementComp = GetOwner()->FindComponentByClass<UCharacterMovementComponent>())
		{
			CharacterPropertiesBackup.bAllowPhysicsRotationDuringAnimRootMotion = MovementComp->bAllowPhysicsRotationDuringAnimRootMotion;
			CharacterPropertiesBackup.bUseControllerDesiredRotation = MovementComp->bUseControllerDesiredRotation;
			CharacterPropertiesBackup.bOrientRotationToMovement = MovementComp->bOrientRotationToMovement;
			MovementComp->bAllowPhysicsRotationDuringAnimRootMotion = false;
			MovementComp->bUseControllerDesiredRotation = false;
			MovementComp->bOrientRotationToMovement = false;
		}

		OnJoinedSceneDelegate.Broadcast(this);
	}
}

void UContextualAnimSceneActorComponent::OnLeftScene()
{
	UE_LOG(LogContextualAnim, Verbose, TEXT("%-21s UContextualAnimSceneActorComponent::OnLeftScene Actor: %s Current Bindings Id: %d"),
		*UEnum::GetValueAsString(TEXT("Engine.ENetRole"), GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()), Bindings.GetID());

	if (const FContextualAnimSceneBinding* Binding = Bindings.FindBindingByActor(GetOwner()))
	{
		// Stop listening to TickPose if we were
		USkeletalMeshComponent* SkelMeshComp = UContextualAnimUtilities::TryGetSkeletalMeshComponent(GetOwner());
		if (SkelMeshComp && SkelMeshComp->OnTickPose.IsBoundToObject(this))
		{
			SkelMeshComp->OnTickPose.RemoveAll(this);
		}

		// Restore collision between actors
		// Note that this assumes that we are the only one disabling the collision between these actors. 
		// We might want to add a more robust mechanism to avoid overriding a request to disable collision that may have been set by another system
		SetIgnoreCollisionWithOtherActors(false);

		// Restore bAllowPhysicsRotationDuringAnimRootMotion
		if (UCharacterMovementComponent* MovementComp = GetOwner()->FindComponentByClass<UCharacterMovementComponent>())
		{
			MovementComp->bAllowPhysicsRotationDuringAnimRootMotion = CharacterPropertiesBackup.bAllowPhysicsRotationDuringAnimRootMotion;
			MovementComp->bUseControllerDesiredRotation = CharacterPropertiesBackup.bUseControllerDesiredRotation;
			MovementComp->bOrientRotationToMovement = CharacterPropertiesBackup.bOrientRotationToMovement;
		}

		OnLeftSceneDelegate.Broadcast(this);

		Bindings.Reset();
	}
}

void UContextualAnimSceneActorComponent::JoinScene(const FContextualAnimSceneBindings& InBindings, const TArray<FContextualAnimWarpTarget>& WarpTargets)
{
	if (Bindings.IsValid())
	{
		LeaveScene();
	}

	if (const FContextualAnimSceneBinding* Binding = InBindings.FindBindingByActor(GetOwner()))
	{
		UE_LOG(LogContextualAnim, Verbose, TEXT("%-21s UContextualAnimSceneActorComponent::JoinScene Actor: %s Role: %s InBindings Id: %d Section: %d Asset: %s"),
			*UEnum::GetValueAsString(TEXT("Engine.ENetRole"), GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()), *InBindings.GetRoleFromBinding(*Binding).ToString(), InBindings.GetID(), InBindings.GetSectionIdx(), *GetNameSafe(InBindings.GetSceneAsset()));

		Bindings = InBindings;

		for (const FContextualAnimWarpTarget& WarpTarget : WarpTargets)
		{
			Bindings.SetRoleWarpTarget(WarpTarget.Role, WarpTarget.TargetName, WarpTarget.TargetTransform);
		}

		const FContextualAnimTrack& AnimTrack = Bindings.GetAnimTrackFromBinding(*Binding);
		PlayAnimation_Internal(AnimTrack.Animation, 0.f, true);

		AddOrUpdateWarpTargets(AnimTrack.SectionIdx, AnimTrack.AnimSetIdx);

		// Disable collision between actors so they can align perfectly
		SetIgnoreCollisionWithOtherActors(true);

		SetMovementState(AnimTrack.bRequireFlyingMode);

		OnJoinedSceneDelegate.Broadcast(this);
	}
}

void UContextualAnimSceneActorComponent::LeaveScene()
{
	if (const FContextualAnimSceneBinding* Binding = Bindings.FindBindingByActor(GetOwner()))
	{
		UE_LOG(LogContextualAnim, Verbose, TEXT("%-21s UContextualAnimSceneActorComponent::LeaveScene Actor: %s Role: %s Current Bindings Id: %d Section: %d Asset: %s"),
			*UEnum::GetValueAsString(TEXT("Engine.ENetRole"), GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()), *Bindings.GetRoleFromBinding(*Binding).ToString(),
			Bindings.GetID(), Bindings.GetSectionIdx(), *GetNameSafe(Bindings.GetSceneAsset()));

		const FContextualAnimTrack& AnimTrack = Bindings.GetAnimTrackFromBinding(*Binding);

		if (UAnimInstance* AnimInstance = Binding->GetAnimInstance())
		{
			AnimInstance->OnMontageBlendingOut.RemoveDynamic(this, &UContextualAnimSceneActorComponent::OnMontageBlendingOut);

			//@TODO: Add support for dynamic montage
			UAnimMontage* AnimMontage = Cast<UAnimMontage>(AnimTrack.Animation);

			if (AnimInstance->Montage_IsPlaying(AnimMontage))
			{
				AnimInstance->Montage_Stop(AnimMontage->GetDefaultBlendOutTime(), AnimMontage);
			}
		}

		// Stop listening to TickPose if we were
		USkeletalMeshComponent* SkelMeshComp = Binding->GetSkeletalMeshComponent();
		if (SkelMeshComp && SkelMeshComp->OnTickPose.IsBoundToObject(this))
		{
			SkelMeshComp->OnTickPose.RemoveAll(this);
		}
		
		// Restore collision between actors
		// Note that this assumes that we are the only one disabling the collision between these actors. 
		// We might want to add a more robust mechanism to avoid overriding a request to disable collision that may have been set by another system
		SetIgnoreCollisionWithOtherActors(false);

		RestoreMovementState(AnimTrack.bRequireFlyingMode);

		OnLeftSceneDelegate.Broadcast(this);

		Bindings.Reset();
	}
}

void UContextualAnimSceneActorComponent::SetMovementState(bool bRequireFlyingMode)
{
	if (UCharacterMovementComponent* MovementComp = GetOwner()->FindComponentByClass<UCharacterMovementComponent>())
	{
		// Save movement state before the interaction starts so we can restore it when it ends
		CharacterPropertiesBackup.bIgnoreClientMovementErrorChecksAndCorrection = MovementComp->bIgnoreClientMovementErrorChecksAndCorrection;
		CharacterPropertiesBackup.bAllowPhysicsRotationDuringAnimRootMotion = MovementComp->bAllowPhysicsRotationDuringAnimRootMotion;
		CharacterPropertiesBackup.bUseControllerDesiredRotation = MovementComp->bUseControllerDesiredRotation;
		CharacterPropertiesBackup.bOrientRotationToMovement = MovementComp->bOrientRotationToMovement;

		// Disable movement correction.
		MovementComp->bIgnoreClientMovementErrorChecksAndCorrection = true;

		// Prevent physics rotation. During the interaction we want to be fully root motion driven
		MovementComp->bAllowPhysicsRotationDuringAnimRootMotion = false;
		MovementComp->bUseControllerDesiredRotation = false;
		MovementComp->bOrientRotationToMovement = false;

		//@TODO: Temp solution that assumes these interactions are not locally predicted and that is ok to be in flying mode during the entire animation
		if (bRequireFlyingMode && MovementComp->MovementMode != MOVE_Flying)
		{
			MovementComp->SetMovementMode(MOVE_Flying);
		}
	}
}

void UContextualAnimSceneActorComponent::RestoreMovementState(bool bRequireFlyingMode)
{
	if (UCharacterMovementComponent* MovementComp = GetOwner()->FindComponentByClass<UCharacterMovementComponent>())
	{
		// Restore movement state
		MovementComp->bIgnoreClientMovementErrorChecksAndCorrection = CharacterPropertiesBackup.bIgnoreClientMovementErrorChecksAndCorrection;
		MovementComp->bAllowPhysicsRotationDuringAnimRootMotion = CharacterPropertiesBackup.bAllowPhysicsRotationDuringAnimRootMotion;
		MovementComp->bUseControllerDesiredRotation = CharacterPropertiesBackup.bUseControllerDesiredRotation;
		MovementComp->bOrientRotationToMovement = CharacterPropertiesBackup.bOrientRotationToMovement;

		//@TODO: Temp solution that assumes these interactions are not locally predicted and that is ok to be in flying mode during the entire animation
		if (bRequireFlyingMode && MovementComp->MovementMode == MOVE_Flying)
		{
			MovementComp->SetMovementMode(MOVE_Walking);
		}
	}
}

void UContextualAnimSceneActorComponent::OnMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted)
{
	if (bGuardAnimEvents)
	{
		return;
	}

	UE_LOG(LogContextualAnim, Verbose, TEXT("%-21s UContextualAnimSceneActorComponent::OnMontageBlendingOut Actor: %s Montage: %s bInterrupted: %d"),
		*UEnum::GetValueAsString(TEXT("Engine.ENetRole"), GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()), *GetNameSafe(Montage), bInterrupted);

	if (const FContextualAnimSceneBinding* Binding = Bindings.FindBindingByActor(GetOwner()))
	{
		LeaveScene();

		if (GetOwner()->HasAuthority())
		{
			// Rep empty bindings if we were the initiator of this interaction.
			if (RepBindings.IsValid())
			{
				RepBindings.Clear();
				MARK_PROPERTY_DIRTY_FROM_NAME(UContextualAnimSceneActorComponent, RepBindings, this);

				GetOwner()->ForceNetUpdate();
			}
		}
	}
}

void UContextualAnimSceneActorComponent::OnTickPose(class USkinnedMeshComponent* SkinnedMeshComponent, float DeltaTime, bool bNeedsValidRootMotion)
{
	//@TODO: Check for LOD to prevent this update if the actor is too far away
	UpdateIKTargets();
}

void UContextualAnimSceneActorComponent::UpdateIKTargets()
{
	IKTargets.Reset();

	const FContextualAnimSceneBinding* BindingPtr = Bindings.FindBindingByActor(GetOwner());
	if (BindingPtr == nullptr)
	{
		return;
	}

	const FAnimMontageInstance* MontageInstance = BindingPtr->GetAnimMontageInstance();
	if(MontageInstance == nullptr)
	{
		return;
	}

	const TArray<FContextualAnimIKTargetDefinition>& IKTargetDefs = Bindings.GetIKTargetDefContainerFromBinding(*BindingPtr).IKTargetDefs;
	for (const FContextualAnimIKTargetDefinition& IKTargetDef : IKTargetDefs)
	{
		float Alpha = UAnimNotifyState_IKWindow::GetIKAlphaValue(IKTargetDef.GoalName, MontageInstance);

		// @TODO: IKTargetTransform will be off by 1 frame if we tick before target. 
		// Should we at least add an option to the SceneAsset to setup tick dependencies or should this be entirely up to the user?

		if (const FContextualAnimSceneBinding* TargetBinding = Bindings.FindBindingByRole(IKTargetDef.TargetRoleName))
		{
			// Do not update if the target actor should be playing and animation but its not yet. 
			// This could happen in multi player when the initiator start playing the animation locally
			const UAnimSequenceBase* TargetAnimation = Bindings.GetAnimTrackFromBinding(*TargetBinding).Animation;
			if (TargetAnimation)
			{
				//@TODO: Add support for dynamic montages
				const FAnimMontageInstance* TargetMontageInstance = TargetBinding->GetAnimMontageInstance();
				if (!TargetMontageInstance || TargetMontageInstance->Montage != TargetAnimation)
				{
					Alpha = 0.f;
				}
			}

			if (Alpha > 0.f)
			{
				if (const USkeletalMeshComponent* TargetSkelMeshComp = TargetBinding->GetSkeletalMeshComponent())
				{
					if (IKTargetDef.Provider == EContextualAnimIKTargetProvider::Autogenerated)
					{
						const FTransform IKTargetParentTransform = TargetSkelMeshComp->GetSocketTransform(IKTargetDef.TargetBoneName);

						const float Time = MontageInstance->GetPosition();
						const FTransform IKTargetTransform = Bindings.GetIKTargetTransformFromBinding(*BindingPtr, IKTargetDef.GoalName, Time) * IKTargetParentTransform;

						IKTargets.Add(FContextualAnimIKTarget(IKTargetDef.GoalName, Alpha, IKTargetTransform));

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
						if (CVarContextualAnimIKDebug.GetValueOnGameThread() > 0)
						{
							const float DrawDebugDuration = CVarContextualAnimIKDrawDebugLifetime.GetValueOnGameThread();
							DrawDebugLine(GetWorld(), IKTargetParentTransform.GetLocation(), IKTargetTransform.GetLocation(), FColor::MakeRedToGreenColorFromScalar(Alpha), false, DrawDebugDuration, 0, 0.5f);
							DrawDebugCoordinateSystem(GetWorld(), IKTargetTransform.GetLocation(), IKTargetTransform.Rotator(), 10.f, false, DrawDebugDuration, 0, 0.5f);
						}
#endif
					}
					else if (IKTargetDef.Provider == EContextualAnimIKTargetProvider::Bone)
					{
						const FTransform IKTargetTransform = TargetSkelMeshComp->GetSocketTransform(IKTargetDef.TargetBoneName);

						IKTargets.Add(FContextualAnimIKTarget(IKTargetDef.GoalName, Alpha, IKTargetTransform));

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
						if (CVarContextualAnimIKDebug.GetValueOnGameThread() > 0)
						{
							const float DrawDebugDuration = CVarContextualAnimIKDrawDebugLifetime.GetValueOnGameThread();
							const FTransform IKTargetParentTransform = TargetSkelMeshComp->GetSocketTransform(TargetSkelMeshComp->GetParentBone(IKTargetDef.TargetBoneName));
							DrawDebugLine(GetWorld(), IKTargetParentTransform.GetLocation(), IKTargetTransform.GetLocation(), FColor::MakeRedToGreenColorFromScalar(Alpha), false, DrawDebugDuration, 0, 0.5f);
							DrawDebugCoordinateSystem(GetWorld(), IKTargetTransform.GetLocation(), IKTargetTransform.Rotator(), 10.f, false, DrawDebugDuration, 0, 0.5f);
						}
#endif
					}
				}
			}
		}
	}
}

void UContextualAnimSceneActorComponent::AddIKGoals_Implementation(TMap<FName, FIKRigGoal>& OutGoals)
{
	OutGoals.Reserve(IKTargets.Num());

	for(const FContextualAnimIKTarget& IKTarget : IKTargets)
	{
		FIKRigGoal Goal;
		Goal.Name = IKTarget.GoalName;
		Goal.Position = IKTarget.Transform.GetLocation();
		Goal.Rotation = IKTarget.Transform.Rotator();
		Goal.PositionAlpha = IKTarget.Alpha;
		Goal.RotationAlpha = IKTarget.Alpha;
		Goal.PositionSpace = EIKRigGoalSpace::World;
		Goal.RotationSpace = EIKRigGoalSpace::World;
		OutGoals.Add(Goal.Name, Goal);
	}
}

const FContextualAnimIKTarget& UContextualAnimSceneActorComponent::GetIKTargetByGoalName(FName GoalName) const
{
	const FContextualAnimIKTarget* IKTargetPtr = IKTargets.FindByPredicate([GoalName](const FContextualAnimIKTarget& IKTarget){
		return IKTarget.GoalName == GoalName;
	});

	return IKTargetPtr ? *IKTargetPtr : FContextualAnimIKTarget::InvalidIKTarget;
}

FPrimitiveSceneProxy* UContextualAnimSceneActorComponent::CreateSceneProxy()
{
	class FSceneActorCompProxy final : public FPrimitiveSceneProxy
	{
	public:

		SIZE_T GetTypeHash() const override
		{
			static size_t UniquePointer;
			return reinterpret_cast<size_t>(&UniquePointer);
		}

		FSceneActorCompProxy(const UContextualAnimSceneActorComponent* InComponent)
			: FPrimitiveSceneProxy(InComponent)
			, SceneAssetPtr(InComponent->SceneAsset)
		{
		}

		virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
		{
			const UContextualAnimSceneAsset* Asset = SceneAssetPtr.Get();
			if (Asset == nullptr)
			{
				return;
			}

			const FMatrix& LocalToWorld = GetLocalToWorld();
			const FTransform ToWorldTransform = FTransform(LocalToWorld);

			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
			{
				if (VisibilityMap & (1 << ViewIndex))
				{
					const FSceneView* View = Views[ViewIndex];

					// Taking into account the min and maximum drawing distance
					const float DistanceSqr = (View->ViewMatrices.GetViewOrigin() - LocalToWorld.GetOrigin()).SizeSquared();
					if (DistanceSqr < FMath::Square(GetMinDrawDistance()) || DistanceSqr > FMath::Square(GetMaxDrawDistance()))
					{
						continue;
					}

					FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);

					//DrawCircle(PDI, ToWorldTransform.GetLocation(), FVector(1, 0, 0), FVector(0, 1, 0), FColor::Red, SceneAssetPtr->GetRadius(), 12, SDPG_World, 1.f);

					SceneAssetPtr->ForEachAnimTrack([=](const FContextualAnimTrack& AnimTrack)
					{
						if (AnimTrack.Role != SceneAssetPtr->GetPrimaryRole())
						{
							// Draw Entry Point
							const FTransform EntryTransform = (SceneAssetPtr->GetAlignmentTransform(AnimTrack, 0, 0.f) * ToWorldTransform);
							DrawCoordinateSystem(PDI, EntryTransform.GetLocation(), EntryTransform.Rotator(), 20.f, SDPG_World, 3.f);

							// Draw Sync Point
							const FTransform SyncPoint = SceneAssetPtr->GetAlignmentTransform(AnimTrack, 0, AnimTrack.GetSyncTimeForWarpSection(0)) * ToWorldTransform;
							DrawCoordinateSystem(PDI, SyncPoint.GetLocation(), SyncPoint.Rotator(), 20.f, SDPG_World, 3.f);

							FLinearColor DrawColor = FLinearColor::White;
							for (const UContextualAnimSelectionCriterion* Criterion : AnimTrack.SelectionCriteria)
							{
								if (const UContextualAnimSelectionCriterion_TriggerArea* Spatial = Cast<UContextualAnimSelectionCriterion_TriggerArea>(Criterion))
								{
									const float HalfHeight = Spatial->Height / 2.f;
									const int32 LastIndex = Spatial->PolygonPoints.Num() - 1;
									for (int32 Idx = 0; Idx <= LastIndex; Idx++)
									{
										const FVector P0 = ToWorldTransform.TransformPositionNoScale(Spatial->PolygonPoints[Idx]);
										const FVector P1 = ToWorldTransform.TransformPositionNoScale(Spatial->PolygonPoints[Idx == LastIndex ? 0 : Idx + 1]);

										PDI->DrawLine(P0, P1, DrawColor, SDPG_Foreground, 2.f);
										PDI->DrawLine(P0 + FVector::UpVector * Spatial->Height, P1 + FVector::UpVector * Spatial->Height, DrawColor, SDPG_Foreground, 2.f);

										PDI->DrawLine(P0, P0 + FVector::UpVector * Spatial->Height, DrawColor, SDPG_Foreground, 2.f);
									}
								}
							}
						}

						return UE::ContextualAnim::EForEachResult::Continue;
					});
				}
			}
		}

		virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
		{
			const bool bShowForCollision = View->Family->EngineShowFlags.Collision;
			FPrimitiveViewRelevance Result;
			Result.bDrawRelevance = IsShown(View);
			Result.bDynamicRelevance = true;
			Result.bNormalTranslucency = Result.bSeparateTranslucency = IsShown(View);
			return Result;
		}

		virtual uint32 GetMemoryFootprint(void) const override
		{
			return(sizeof(*this) + GetAllocatedSize());
		}

		uint32 GetAllocatedSize(void) const
		{
			return(FPrimitiveSceneProxy::GetAllocatedSize());
		}

	private:
		TWeakObjectPtr<const UContextualAnimSceneAsset> SceneAssetPtr;
	};

	if(bEnableDebug)
	{
		return new FSceneActorCompProxy(this);
	}

	return nullptr;
}
