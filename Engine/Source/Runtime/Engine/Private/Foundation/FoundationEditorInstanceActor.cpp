// Copyright Epic Games, Inc. All Rights Reserved.

#include "Foundation/FoundationEditorInstanceActor.h"
#include "Foundation/FoundationActor.h"
#include "Foundation/FoundationSubsystem.h"
#include "Engine/World.h"

AFoundationEditorInstanceActor::AFoundationEditorInstanceActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITOR
	, FoundationID(InvalidFoundationID)
#endif
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
	RootComponent->Mobility = EComponentMobility::Static;
}

#if WITH_EDITOR
AActor* AFoundationEditorInstanceActor::GetSelectionParent() const
{
	if (UFoundationSubsystem* FoundationSubsystem = GetWorld()->GetSubsystem<UFoundationSubsystem>())
	{
		return FoundationSubsystem->GetFoundation(FoundationID);
	}

	return nullptr;
}

AFoundationEditorInstanceActor* AFoundationEditorInstanceActor::Create(AFoundationActor* FoundationActor, ULevel* LoadedLevel)
{
	FActorSpawnParameters SpawnParams;
	SpawnParams.OverrideLevel = LoadedLevel;
	SpawnParams.bHideFromSceneOutliner = true;
	SpawnParams.bCreateActorPackage = false;
	SpawnParams.ObjectFlags = RF_Transient;
	SpawnParams.bNoFail = true;
	AFoundationEditorInstanceActor* InstanceActor = FoundationActor->GetWorld()->SpawnActor<AFoundationEditorInstanceActor>(FoundationActor->GetActorLocation(), FoundationActor->GetActorRotation(), SpawnParams);

	InstanceActor->SetFoundationID(FoundationActor->GetFoundationID());
	
	for (AActor* LevelActor : LoadedLevel->Actors)
	{
		if (LevelActor && LevelActor->GetAttachParentActor() == nullptr && !LevelActor->IsChildActor() && LevelActor != InstanceActor)
		{
			LevelActor->AttachToActor(InstanceActor, FAttachmentTransformRules::KeepWorldTransform);
		}
	}

	return InstanceActor;
}

#endif