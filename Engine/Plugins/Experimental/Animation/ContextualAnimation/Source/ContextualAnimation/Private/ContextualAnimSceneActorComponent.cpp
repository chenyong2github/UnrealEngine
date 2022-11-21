// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimSceneActorComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ContextualAnimSelectionCriterion.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "ContextualAnimManager.h"
#include "ContextualAnimSceneAsset.h"
#include "ContextualAnimUtilities.h"
#include "AnimNotifyState_IKWindow.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"
#include "PrimitiveSceneProxy.h"
#include "SceneManagement.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ContextualAnimSceneActorComponent)

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
TAutoConsoleVariable<int32> CVarContextualAnimIKDebug(TEXT("a.ContextualAnim.IK.Debug"), 0, TEXT("Draw Debug IK Targets"));
TAutoConsoleVariable<float> CVarContextualAnimIKDrawDebugLifetime(TEXT("a.ContextualAnim.IK.DrawDebugLifetime"), 0, TEXT("Draw Debug Duration"));
TAutoConsoleVariable<float> CVarContextualAnimIKForceAlpha(TEXT("a.ContextualAnim.IK.ForceAlpha"), -1.f, TEXT("Override Alpha value for all the targets. -1 = Disable"));
#endif

UContextualAnimSceneActorComponent::UContextualAnimSceneActorComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(true);
}

void UContextualAnimSceneActorComponent::GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	Params.Condition = COND_SimulatedOnly;
	DOREPLIFETIME_WITH_PARAMS_FAST(UContextualAnimSceneActorComponent, RepBindings, Params);
}

bool UContextualAnimSceneActorComponent::StartContextualAnimScene(const FContextualAnimSceneBindings& InBindings)
{
	UE_LOG(LogContextualAnim, Log, TEXT("%-21s UContextualAnimSceneActorComponent::StartContextualAnim Actor: %s"), *UEnum::GetValueAsString(TEXT("Engine.ENetRole"), GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()));

	const FContextualAnimSceneBinding* OwnerBinding = InBindings.FindBindingByActor(GetOwner());
	if (ensureAlways(OwnerBinding))
	{
		OwnerBinding->GetSceneActorComponent()->JoinScene(InBindings);

		for (const FContextualAnimSceneBinding& Binding : InBindings)
		{
			if (Binding.GetActor() != GetOwner())
			{
				Binding.GetSceneActorComponent()->JoinScene(InBindings);
			}
		}

		//@TODO: Temp until we move the scene pivots to the bindings
		UContextualAnimUtilities::BP_SceneBindings_AddOrUpdateWarpTargetsForBindings(InBindings);

		if (GetOwner()->HasAuthority())
		{
			RepBindings = InBindings;
			MARK_PROPERTY_DIRTY_FROM_NAME(UContextualAnimSceneActorComponent, RepBindings, this);
		}

		return true;
	}

	return false;
}

void UContextualAnimSceneActorComponent::OnRep_Bindings(const FContextualAnimSceneBindings& LastRepBindings)
{
	// @TODO: This need more investigation but for now it prevents an issue caused by this OnRep_ triggering even when there is no (obvious) changein the data
	if(RepBindings.GetID() == LastRepBindings.GetID())
	{
		UE_LOG(LogContextualAnim, Warning, TEXT("%-21s UContextualAnimSceneActorComponent::OnRep_Bindings Actor: %s RepBindings Id: %d LastRepBindings Id: %d"),
			*UEnum::GetValueAsString(TEXT("Engine.ENetRole"), GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()), RepBindings.GetID(), LastRepBindings.GetID());

		return;
	}

	UE_LOG(LogContextualAnim, Verbose, TEXT("%-21s UContextualAnimSceneActorComponent::OnRep_Bindings Actor: %s RepBindings Id: %d Bindings Id: %d"),
		*UEnum::GetValueAsString(TEXT("Engine.ENetRole"), GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()), RepBindings.GetID(), Bindings.GetID());

	// The owner of this component started an interaction on the server
	if (RepBindings.IsValid())
	{
		const FContextualAnimSceneBinding* OwnerBinding = RepBindings.FindBindingByActor(GetOwner());
		if (ensureAlways(OwnerBinding))
		{
			// Join the scene (start playing animation, etc.)
			OwnerBinding->GetSceneActorComponent()->JoinScene(RepBindings);

			// RepBindings is only replicated from the initiator of the actor. 
			// So now we have to tell everyone else involved in the interaction to join us
			// @TODO: For now this assumes that all the actors will start playing the animation at the same time. 
			// We will expand this in the future to allow 'late' join
			for (const FContextualAnimSceneBinding& Binding : RepBindings)
			{
				if (Binding.GetActor() != GetOwner())
				{
					Binding.GetSceneActorComponent()->JoinScene(RepBindings);
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
		LeaveScene();
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
			bAllowPhysicsRotationDuringAnimRootMotionBackup = MovementComp->bAllowPhysicsRotationDuringAnimRootMotion;
			MovementComp->bAllowPhysicsRotationDuringAnimRootMotion = false;
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
			MovementComp->bAllowPhysicsRotationDuringAnimRootMotion = bAllowPhysicsRotationDuringAnimRootMotionBackup;
		}

		OnLeftSceneDelegate.Broadcast(this);

		Bindings.Reset();
	}
}

void UContextualAnimSceneActorComponent::JoinScene(const FContextualAnimSceneBindings& InBindings)
{
	if (Bindings.IsValid())
	{
		LeaveScene();
	}

	if (const FContextualAnimSceneBinding* Binding = InBindings.FindBindingByActor(GetOwner()))
	{
		UE_LOG(LogContextualAnim, Verbose, TEXT("%-21s UContextualAnimSceneActorComponent::JoinScene Actor: %s InBindings Id: %d Section: %d Asset: %s"),
			*UEnum::GetValueAsString(TEXT("Engine.ENetRole"), GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()), InBindings.GetID(), InBindings.GetSectionIdx(), *GetNameSafe(InBindings.GetSceneAsset()));

		Bindings = InBindings;

		const FContextualAnimTrack& AnimTrack = Bindings.GetAnimTrackFromBinding(*Binding);

		if (UAnimInstance* AnimInstance = Binding->GetAnimInstance())
		{
			AnimInstance->OnMontageBlendingOut.AddUniqueDynamic(this, &UContextualAnimSceneActorComponent::OnMontageBlendingOut);

			//@TODO: Add support for dynamic montage
			UAnimMontage* AnimMontage = Cast<UAnimMontage>(AnimTrack.Animation);
			AnimInstance->Montage_Play(AnimMontage, 1.f);
		}

		USkeletalMeshComponent* SkelMeshComp = Binding->GetSkeletalMeshComponent();
		if (SkelMeshComp && !SkelMeshComp->OnTickPose.IsBoundToObject(this))
		{
			SkelMeshComp->OnTickPose.AddUObject(this, &UContextualAnimSceneActorComponent::OnTickPose);
		}

		// Disable collision between actors so they can align perfectly
		SetIgnoreCollisionWithOtherActors(true);

		// Prevent physics rotation. During the interaction we want to be fully root motion driven
		if (UCharacterMovementComponent* MovementComp = GetOwner()->FindComponentByClass<UCharacterMovementComponent>())
		{
			bAllowPhysicsRotationDuringAnimRootMotionBackup = MovementComp->bAllowPhysicsRotationDuringAnimRootMotion;
			MovementComp->bAllowPhysicsRotationDuringAnimRootMotion = false;

			//@TODO: Temp solution that assumes these interactions are not locally predicted and that is ok to be in flying mode during the entire animation
			if (AnimTrack.bRequireFlyingMode && MovementComp->MovementMode != MOVE_Flying)
			{
				MovementComp->SetMovementMode(MOVE_Flying);
			}
		}

		OnJoinedSceneDelegate.Broadcast(this);
	}
}

void UContextualAnimSceneActorComponent::LeaveScene()
{
	if (const FContextualAnimSceneBinding* Binding = Bindings.FindBindingByActor(GetOwner()))
	{
		UE_LOG(LogContextualAnim, Verbose, TEXT("%-21s UContextualAnimSceneActorComponent::LeaveScene Actor: %s Current Bindings Id: %d Section: %d Asset: %s"),
			*UEnum::GetValueAsString(TEXT("Engine.ENetRole"), GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()), Bindings.GetID(), Bindings.GetSectionIdx(), *GetNameSafe(Bindings.GetSceneAsset()));

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

		// Restore bAllowPhysicsRotationDuringAnimRootMotion
		if (UCharacterMovementComponent* MovementComp = GetOwner()->FindComponentByClass<UCharacterMovementComponent>())
		{
			MovementComp->bAllowPhysicsRotationDuringAnimRootMotion = bAllowPhysicsRotationDuringAnimRootMotionBackup;

			//@TODO: Temp solution that assumes these interactions are not locally predicted and that is ok to be in flying mode during the entire animation
			if (AnimTrack.bRequireFlyingMode && MovementComp->MovementMode == MOVE_Flying)
			{
				MovementComp->SetMovementMode(MOVE_Walking);
			}
		}

		OnLeftSceneDelegate.Broadcast(this);

		Bindings.Reset();
	}
}

void UContextualAnimSceneActorComponent::OnMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted)
{
	UE_LOG(LogContextualAnim, Verbose, TEXT("%-21s UContextualAnimSceneActorComponent::OnMontageBlendingOut Actor: %s Montage: %s bInterrupted: %d"),
		*UEnum::GetValueAsString(TEXT("Engine.ENetRole"), GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()), *GetNameSafe(Montage), bInterrupted);

	if (const FContextualAnimSceneBinding* Binding = Bindings.FindBindingByActor(GetOwner()))
	{
		if (Bindings.GetAnimTrackFromBinding(*Binding).Animation == Montage)
		{
			LeaveScene();

			if (GetOwner()->HasAuthority())
			{
				// Rep empty bindings if we were the initiator of this interaction.
				if(RepBindings.IsValid())
				{
					RepBindings.Reset();
					MARK_PROPERTY_DIRTY_FROM_NAME(UContextualAnimSceneActorComponent, RepBindings, this);
				}

				//@TODO: Replicate this event separately for each other member of the interaction
			}
		}
	}
}

void UContextualAnimSceneActorComponent::OnTickPose(class USkinnedMeshComponent* SkinnedMeshComponent, float DeltaTime, bool bNeedsValidRootMotion)
{
	if (const FContextualAnimSceneBinding* Binding = Bindings.FindBindingByActor(GetOwner()))
	{
		// Synchronize playback time with the leader
		FAnimMontageInstance* MontageInstance = Binding->GetAnimMontageInstance();
		if (MontageInstance && MontageInstance->GetMontageSyncLeader() == nullptr)
		{
			if (const FContextualAnimSceneBinding* SyncLeader = Bindings.GetSyncLeader())
			{
				if (SyncLeader->GetActor() != GetOwner())
				{
					if (FAnimMontageInstance* LeaderMontageInstance = SyncLeader->GetAnimMontageInstance())
					{
						if (LeaderMontageInstance->Montage == Bindings.GetAnimTrackFromBinding(*SyncLeader).Animation &&
							MontageInstance->Montage == Bindings.GetAnimTrackFromBinding(*Binding).Animation)
						{
							MontageInstance->MontageSync_Follow(LeaderMontageInstance);
						}
					}
				}
			}
		}

		//@TODO: Check for LOD to prevent this update if the actor is too far away
		UpdateIKTargets();
	}
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
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		const bool bDrawDebugEnable = CVarContextualAnimIKDebug.GetValueOnGameThread() > 0;
		const float DrawDebugDuration = CVarContextualAnimIKDrawDebugLifetime.GetValueOnGameThread();
		FTransform IKTargetParentTransformForDebug = FTransform::Identity;
#endif

		FTransform IKTargetTransform = FTransform::Identity;

		float Alpha = UAnimNotifyState_IKWindow::GetIKAlphaValue(IKTargetDef.GoalName, MontageInstance);

		// @TODO: IKTargetTransform will be off by 1 frame if we tick before target. 
		// Should we at least add an option to the SceneAsset to setup tick dependencies or should this be entirely up to the user?

		if (const FContextualAnimSceneBinding* TargetBinding = Bindings.FindBindingByRole(IKTargetDef.TargetRoleName))
		{
			if (const USkeletalMeshComponent* TargetSkelMeshComp = TargetBinding->GetSkeletalMeshComponent())
			{
				if (IKTargetDef.Provider == EContextualAnimIKTargetProvider::Autogenerated)
				{
					const FTransform IKTargetParentTransform = TargetSkelMeshComp->GetSocketTransform(IKTargetDef.TargetBoneName);

					const float Time = MontageInstance->GetPosition();
					IKTargetTransform = Bindings.GetIKTargetTransformFromBinding(*BindingPtr, IKTargetDef.GoalName, Time) * IKTargetParentTransform;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
					if (bDrawDebugEnable)
					{
						IKTargetParentTransformForDebug = IKTargetParentTransform;
					}
#endif
				}
				else if (IKTargetDef.Provider == EContextualAnimIKTargetProvider::Bone)
				{
					IKTargetTransform = TargetSkelMeshComp->GetSocketTransform(IKTargetDef.TargetBoneName);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
					if (bDrawDebugEnable)
					{
						IKTargetParentTransformForDebug = TargetSkelMeshComp->GetSocketTransform(TargetSkelMeshComp->GetParentBone(IKTargetDef.TargetBoneName));
					}
#endif
				}
			}
		}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		const float ForcedAlphaValue = CVarContextualAnimIKForceAlpha.GetValueOnGameThread();
		if (ForcedAlphaValue > 0.f)
		{
			Alpha = FMath::Clamp(ForcedAlphaValue, 0.f, 1.f);
		}

		if (bDrawDebugEnable)
		{
			const float DrawThickness = 0.5f;
			const FColor DrawColor = FColor::MakeRedToGreenColorFromScalar(Alpha);

			DrawDebugLine(GetWorld(), IKTargetParentTransformForDebug.GetLocation(), IKTargetTransform.GetLocation(), DrawColor, false, DrawDebugDuration, 0, DrawThickness);
			DrawDebugCoordinateSystem(GetWorld(), IKTargetTransform.GetLocation(), IKTargetTransform.Rotator(), 10.f, false, DrawDebugDuration, 0, DrawThickness);
			//DrawDebugSphere(GetWorld(), IKTargetTransform.GetLocation(), 5.f, 12, DrawColor, false, 0.f, 0, DrawThickness  );

			//DrawDebugString(GetWorld(), IKTargetTransform.GetLocation(), FString::Printf(TEXT("%s (%f)"), *IKTargetDef.AlphaCurveName.ToString(), Alpha));
		}
#endif

		// Convert IK Target to mesh space
		//IKTargetTransform = IKTargetTransform.GetRelativeTransform(BindingPtr->GetSkeletalMeshComponent()->GetComponentTransform());

		IKTargets.Add(FContextualAnimIKTarget(IKTargetDef.GoalName, Alpha, IKTargetTransform));
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
							const FTransform EntryTransform = (AnimTrack.GetAlignmentTransformAtEntryTime() * ToWorldTransform);
							DrawCoordinateSystem(PDI, EntryTransform.GetLocation(), EntryTransform.Rotator(), 20.f, SDPG_World, 3.f);

							// Draw Sync Point
							const FTransform SyncPoint = AnimTrack.GetAlignmentTransformAtSyncTime() * ToWorldTransform;
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
