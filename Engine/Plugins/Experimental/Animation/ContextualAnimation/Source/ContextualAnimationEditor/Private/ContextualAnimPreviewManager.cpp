// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimPreviewManager.h"
#include "ContextualAnimSceneAsset.h"
#include "ContextualAnimEdMode.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "AIController.h"

UContextualAnimPreviewManager::UContextualAnimPreviewManager(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

}

void UContextualAnimPreviewManager::MoveForward(float Value)
{
	if (TestCharacter.IsValid())
	{
		const FVector WorldDirection = FRotationMatrix(TestCharacter->GetActorRotation()).GetScaledAxis(EAxis::X);
		TestCharacter->AddMovementInput(WorldDirection, Value);
	}
}

void UContextualAnimPreviewManager::MoveRight(float Value)
{
	if(TestCharacter.IsValid())
	{
		const FVector WorldDirection = FRotationMatrix(TestCharacter->GetActorRotation()).GetScaledAxis(EAxis::Y);
		TestCharacter->AddMovementInput(WorldDirection, Value);
	}
}

void UContextualAnimPreviewManager::MoveToLocation(const FVector& GoalLocation)
{
	AAIController* Controller = TestCharacter.IsValid() ? Cast<AAIController>(TestCharacter->GetController()) : nullptr;
	if (Controller)
	{
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Controller->GetWorld());
		const ANavigationData* NavData = NavSys ? NavSys->GetNavDataForProps(Controller->GetNavAgentPropertiesRef(), Controller->GetNavAgentLocation()) : nullptr;

		const bool bUsePathfinding = (NavData != nullptr);
		Controller->MoveToLocation(GoalLocation, 10.f, true, bUsePathfinding);
	}
}

void UContextualAnimPreviewManager::SpawnPreviewActors(const UContextualAnimSceneAsset* SceneAsset, const FTransform& SceneOrigin)
{
	if (SceneAsset)
	{
		PreviewActors.Reset();

		//DrawDebugCoordinateSystem(GetWorld(), ToWorldTransform.GetLocation(), ToWorldTransform.Rotator(), 20.f, false, 10.f, 0, 1.f);

		for (const auto& Entry : SceneAsset->DataContainer)
		{
			const FName& Role = Entry.Key;
			const FContextualAnimData& Data = Entry.Value.AnimData;

			const FTransform SpawnTransform = (Data.AlignmentData.ExtractTransformAtTime(0, 0.f) * SceneOrigin);

			UClass* PreviewClass = SceneAsset->GetPreviewActorClassForRole(Role);
			if(!PreviewClass && DefaultPreviewClass)
			{
				PreviewClass = DefaultPreviewClass.Get();
			}

			AActor* PreviewActor = SpawnPreviewActor(PreviewClass, SpawnTransform);
			if (PreviewActor)
			{
				PreviewActors.Add(Role, PreviewActor);

				if (!TestCharacter.IsValid())
				{
					TestCharacter = Cast<ACharacter>(PreviewActor);
				}
			}
		}
	}
}

AActor* UContextualAnimPreviewManager::SpawnPreviewActor(UClass* Class, const FTransform& SpawnTransform) const
{
	AActor* PreviewActor = FContextualAnimEdMode::Get().GetWorld()->SpawnActor<AActor>(Class, SpawnTransform);

	if (ACharacter* PreviewCharacter = Cast<ACharacter>(PreviewActor))
	{
		PreviewCharacter->bUseControllerRotationYaw = false;
		if (UCharacterMovementComponent* CharacterMovementComp = PreviewCharacter->GetCharacterMovement())
		{
			CharacterMovementComp->bOrientRotationToMovement = true;
			CharacterMovementComp->bUseControllerDesiredRotation = false;
			CharacterMovementComp->RotationRate = FRotator(0.f, 540.0, 0.f);
		}

		if (!PreviewCharacter->AIControllerClass || !PreviewCharacter->AIControllerClass->IsChildOf<AAIController>())
		{
			PreviewCharacter->AIControllerClass = AAIController::StaticClass();
		}

		PreviewCharacter->SpawnDefaultController();
	}

	return PreviewActor;
}