// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimPreviewManager.h"
#include "ContextualAnimTypes.h"
#include "ContextualAnimSceneAsset.h"
#include "ContextualAnimUtilities.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "MotionWarpingComponent.h"
#include "NavigationSystem.h"
#include "AIController.h"
#include "SkeletalDebugRendering.h"

// FContextualAnimPreviewActorData
//============================================================================================================

void FContextualAnimPreviewActorData::ResetActorTransform(float Time)
{
	AActor* PreviewActor = GetActor();

	const USkeletalMeshComponent* SkelMeshComp = UContextualAnimUtilities::TryGetSkeletalMeshComponent(PreviewActor);

	const FTransform RootTransform = UContextualAnimUtilities::ExtractRootTransformFromAnimation(GetAnimation(), Time);
	const FTransform StartTransform = SkelMeshComp->GetRelativeTransform().Inverse() * RootTransform;

	PreviewActor->SetActorLocationAndRotation(StartTransform.GetLocation(), StartTransform.GetRotation());

	if (UCharacterMovementComponent* MovementComp = PreviewActor->FindComponentByClass<UCharacterMovementComponent>())
	{
		MovementComp->StopMovementImmediately();
	}
}

// UContextualAnimPreviewManager
//============================================================================================================

UContextualAnimPreviewManager::UContextualAnimPreviewManager(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

}

void UContextualAnimPreviewManager::Initialize(UWorld& World, const UContextualAnimSceneAsset& SceneAsset)
{
	WorldPtr = &World;
	SceneAssetPtr = &SceneAsset;
}

UWorld* UContextualAnimPreviewManager::GetWorld() const
{
	return WorldPtr.Get();
}

const UContextualAnimSceneAsset* UContextualAnimPreviewManager::GetSceneAsset() const
{
	check(SceneAssetPtr.IsValid());
	return SceneAssetPtr.Get();
}

FName UContextualAnimPreviewManager::FindRoleByGuid(const FGuid& Guid) const
{ 
	const FContextualAnimPreviewActorData* Result = PreviewActorsData.FindByPredicate([&Guid](const FContextualAnimPreviewActorData& Data) {
		return Data.Guid == Guid;
	});

	return Result ? Result->Role : NAME_None;
}

UAnimMontage* UContextualAnimPreviewManager::FindAnimationByRole(const FName& Role) const
{
	//@TODO: Use the first animation for now
	const FContextualAnimData* DataPtr = GetSceneAsset()->GetAnimDataForRoleAtIndex(Role, 0);
	return DataPtr ? DataPtr->Animation : nullptr;
}

UAnimMontage* UContextualAnimPreviewManager::FindAnimationByGuid(const FGuid& Guid) const
{
	return FindAnimationByRole(FindRoleByGuid(Guid));
}

void UContextualAnimPreviewManager::MoveForward(float Value)
{
	if (ControlledCharacter.IsValid())
	{
		const FVector WorldDirection = FRotationMatrix(ControlledCharacter->GetActorRotation()).GetScaledAxis(EAxis::X);
		ControlledCharacter->AddMovementInput(WorldDirection, Value);
	}
}

void UContextualAnimPreviewManager::MoveRight(float Value)
{
	if(ControlledCharacter.IsValid())
	{
		const FVector WorldDirection = FRotationMatrix(ControlledCharacter->GetActorRotation()).GetScaledAxis(EAxis::Y);
		ControlledCharacter->AddMovementInput(WorldDirection, Value);
	}
}

void UContextualAnimPreviewManager::MoveToLocation(const FVector& GoalLocation)
{
	AAIController* Controller = ControlledCharacter.IsValid() ? Cast<AAIController>(ControlledCharacter->GetController()) : nullptr;
	if (Controller)
	{
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Controller->GetWorld());
		const ANavigationData* NavData = NavSys ? NavSys->GetNavDataForProps(Controller->GetNavAgentPropertiesRef(), Controller->GetNavAgentLocation()) : nullptr;

		const bool bUsePathfinding = (NavData != nullptr);
		Controller->MoveToLocation(GoalLocation, 10.f, true, bUsePathfinding);
	}
}

AActor* UContextualAnimPreviewManager::SpawnPreviewActor(const FName& Role, const FContextualAnimData& Data)
{
	UClass* PreviewClass = GetSceneAsset()->GetTrackSettings(Role)->PreviewActorClass;
	const FTransform SpawnTransform = (Data.AlignmentData.ExtractTransformAtTime(0, 0.f));

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	AActor* PreviewActor = GetWorld()->SpawnActor<AActor>(PreviewClass, SpawnTransform, Params);

	if (ACharacter* PreviewCharacter = Cast<ACharacter>(PreviewActor))
	{
		PreviewCharacter->bUseControllerRotationYaw = false;

		if (UCharacterMovementComponent* CharacterMovementComp = PreviewCharacter->GetCharacterMovement())
		{
			CharacterMovementComp->bOrientRotationToMovement = true;
			CharacterMovementComp->bUseControllerDesiredRotation = false;
			CharacterMovementComp->RotationRate = FRotator(0.f, 540.0, 0.f);
			CharacterMovementComp->bRunPhysicsWithNoController = true;

			CharacterMovementComp->SetMovementMode(Data.bRequireFlyingMode ? EMovementMode::MOVE_Flying : EMovementMode::MOVE_Walking);
		}

		if (UCameraComponent* CameraComp = PreviewCharacter->FindComponentByClass<UCameraComponent>())
		{
			CameraComp->DestroyComponent();
		}
	}

	UE_LOG(LogContextualAnim, Log, TEXT("FContextualAnimViewModel::SpawnPreviewActor. Spawned preview Actor: %s at Loc: %s Rot: %s Role: %s"),
		*GetNameSafe(PreviewActor), *SpawnTransform.GetLocation().ToString(), *SpawnTransform.Rotator().ToString(), *Role.ToString());

	return PreviewActor;
}

void UContextualAnimPreviewManager::AddPreviewActor(AActor& Actor, const FName& Role, const FGuid& Guid, UAnimMontage& Animation)
{
	FContextualAnimPreviewActorData NewData;
	NewData.Actor = &Actor;
	NewData.Role = Role;
	NewData.Guid = Guid;
	NewData.Animation = &Animation;

	PreviewActorsData.Add(NewData);

	if (UAnimInstance* AnimInstance = UContextualAnimUtilities::TryGetAnimInstance(&Actor))
	{
		AnimInstance->Montage_Play(&Animation);
		AnimInstance->Montage_Pause(&Animation);
	}
}

void UContextualAnimPreviewManager::DisableCollisionBetweenActors()
{
	for (const FContextualAnimPreviewActorData& PreviewActorData : PreviewActorsData)
	{
		for (const FContextualAnimPreviewActorData& OtherPreviewActorData : PreviewActorsData)
		{
			if (PreviewActorData.Actor != OtherPreviewActorData.Actor)
			{
				if (ACharacter* PreviewCharacter = Cast<ACharacter>(PreviewActorData.Actor.Get()))
				{
					PreviewCharacter->MoveIgnoreActorAdd(OtherPreviewActorData.Actor.Get());
				}
			}
		}
	}
}

void UContextualAnimPreviewManager::PreviewTimeChanged(EMovieScenePlayerStatus::Type PreviousStatus, float PreviousTime, EMovieScenePlayerStatus::Type CurrentStatus, float CurrentTime, float PlaybackSpeed)
{
	//UE_LOG(LogContextualAnim, Log, TEXT("PreviewTimeChanged PrevStatus: %d CurrStatus: %d PrevTime: %f CurrTime: %f Speed: %f"), (int32)PreviousStatus, (int32)CurrentStatus, PreviousTime, CurrentTime, PlaybackSpeed);

	for (FContextualAnimPreviewActorData& PreviewActorData : PreviewActorsData)
	{
		UAnimMontage* Animation = PreviewActorData.Animation.Get();

		// Ignore static actors
		if(Animation == nullptr)
		{
			continue;
		}

		FAnimMontageInstance* MontageInstance = nullptr;
		if (UAnimInstance* AnimInstance = UContextualAnimUtilities::TryGetAnimInstance(PreviewActorData.GetActor()))
		{
			MontageInstance = AnimInstance->GetActiveMontageInstance();

			// Ensure animation is always playing
			if (MontageInstance == nullptr)
			{
				AnimInstance->Montage_Play(Animation, 1.f, EMontagePlayReturnType::Duration, CurrentTime);
				AnimInstance->Montage_Pause(Animation);
				
				MontageInstance = AnimInstance->GetActiveMontageInstance();
			}
		}

		if(MontageInstance)
		{
			const float AnimPlayLength = Animation->GetPlayLength();
			PreviousTime = FMath::Clamp(PreviousTime, 0.f, AnimPlayLength);
			CurrentTime = FMath::Clamp(CurrentTime, 0.f, AnimPlayLength);

			if (CurrentStatus == EMovieScenePlayerStatus::Stopped || CurrentStatus == EMovieScenePlayerStatus::Scrubbing)
			{
				PreviewActorData.ResetActorTransform(CurrentTime);

				if (MontageInstance->IsPlaying())
				{
					MontageInstance->Pause();
				}

				MontageInstance->SetPosition(CurrentTime);
			}
			else if (CurrentStatus == EMovieScenePlayerStatus::Playing)
			{
				if (PlaybackSpeed > 0.f && CurrentTime < PreviousTime)
				{
					PreviewActorData.ResetActorTransform(CurrentTime);
				}

				if (!MontageInstance->IsPlaying())
				{
					MontageInstance->SetPlaying(true);
				}
			}
		}
	}
}

void UContextualAnimPreviewManager::Reset()
{
	for (const FContextualAnimPreviewActorData& PreviewActorData : PreviewActorsData)
	{
		PreviewActorData.Actor->Destroy();
	}

	PreviewActorsData.Reset();
}

void UContextualAnimPreviewManager::Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
}