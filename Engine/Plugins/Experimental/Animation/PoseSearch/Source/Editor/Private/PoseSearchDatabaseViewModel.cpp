// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchDatabaseViewModel.h"
#include "PoseSearchEditor.h"
#include "PoseSearch/PoseSearch.h"
#include "Modules/ModuleManager.h"
#include "PoseSearchDatabasePreviewScene.h"
#include "Animation/AnimInstance.h"
#include "EngineUtils.h"
#include "PropertyEditorModule.h"
#include "Modules/ModuleManager.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"

FPoseSearchDatabaseViewModel::FPoseSearchDatabaseViewModel()
	: PoseSearchDatabase(nullptr)
{
}

FPoseSearchDatabaseViewModel::~FPoseSearchDatabaseViewModel()
{
}

void FPoseSearchDatabaseViewModel::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(PoseSearchDatabase);
}

void FPoseSearchDatabaseViewModel::Initialize(
	UPoseSearchDatabase* InPoseSearchDatabase,
	const TSharedRef<FPoseSearchDatabasePreviewScene>& InPreviewScene)
{
	PoseSearchDatabase = InPoseSearchDatabase;
	PreviewScenePtr = InPreviewScene;
}

void FPoseSearchDatabaseViewModel::RestartAnimations()
{
	// todo: implement
}

void FPoseSearchDatabaseViewModel::BuildSearchIndex()
{
	PoseSearchDatabase->BeginCacheDerivedData();
}

AActor* FPoseSearchDatabaseViewModel::SpawnPreviewActor(const FPoseSearchDatabaseSequence& DatabaseSequence)
{
	// todo: implement properly

	UClass* PreviewClass = nullptr;
	const FTransform SpawnTransform = FTransform::Identity;

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

			CharacterMovementComp->SetMovementMode(EMovementMode::MOVE_Walking);
		}

		if (UCameraComponent* CameraComp = PreviewCharacter->FindComponentByClass<UCameraComponent>())
		{
			CameraComp->DestroyComponent();
		}
	}

	UE_LOG(LogPoseSearchEditor, Log, TEXT("Spawned preview Actor: %s at Loc: %s Rot: %s"),
		*GetNameSafe(PreviewActor), *SpawnTransform.GetLocation().ToString(), *SpawnTransform.Rotator().ToString());

	return PreviewActor;
}

UWorld* FPoseSearchDatabaseViewModel::GetWorld() const
{
	check(PreviewScenePtr.IsValid());
	return PreviewScenePtr.Pin()->GetWorld();
}

UObject* FPoseSearchDatabaseViewModel::GetPlaybackContext() const
{
	return GetWorld();
}


void FPoseSearchDatabaseViewModel::OnPreviewActorClassChanged()
{
	// todo: implement
}