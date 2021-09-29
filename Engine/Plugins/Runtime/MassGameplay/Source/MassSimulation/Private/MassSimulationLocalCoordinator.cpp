// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassSimulationLocalCoordinator.h"
#include "NavigationSystem.h"
#include "MassProcessor.h"
#include "MassSimulationSubsystem.h"

#if WITH_EDITORONLY_DATA
#include "UObject/ConstructorHelpers.h"
#include "Components/BillboardComponent.h"
#endif // WITH_EDITORONLY_DATA

#if WITH_EDITOR
#endif // WITH_EDITOR

AMassSimulationLocalCoordinator::AMassSimulationLocalCoordinator(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	USceneComponent* SceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SceneComp"));
	RootComponent = SceneComponent;
	RootComponent->Mobility = EComponentMobility::Static;
	
#if WITH_EDITORONLY_DATA
	SpriteComponent = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
	if (!IsRunningCommandlet())
	{
		// Structure to hold one-time initialization
		struct FConstructorStatics
		{
			ConstructorHelpers::FObjectFinderOptional<UTexture2D> NoteTextureObject;
			FName NotesID;
			FText NotesName;
			FConstructorStatics()
				: NoteTextureObject(TEXT("/Engine/EditorResources/S_Note"))
				, NotesID(TEXT("Notes"))
				, NotesName(NSLOCTEXT("SpriteCategory", "Notes", "Notes"))
			{
			}
		};
		static FConstructorStatics ConstructorStatics;
		if (SpriteComponent)
		{
			SpriteComponent->Sprite = ConstructorStatics.NoteTextureObject.Get();
			SpriteComponent->SetRelativeScale3D(FVector(0.5f, 0.5f, 0.5f));
			SpriteComponent->SpriteInfo.Category = ConstructorStatics.NotesID;
			SpriteComponent->SpriteInfo.DisplayName = ConstructorStatics.NotesName;
			SpriteComponent->SetupAttachment(RootComponent);
			SpriteComponent->Mobility = EComponentMobility::Static;
		}
	}
#endif // WITH_EDITORONLY_DATA
	SetCanBeDamaged(false);

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

#if WITH_EDITORONLY_DATA
	bTickInEditor = false;
	bDrawDebug = false;
#endif // WITH_EDITORONLY_DATA
}

void AMassSimulationLocalCoordinator::PostLoad()
{
	Super::PostLoad();
	UE_LOG(LogMass, Warning, TEXT("%s has been loaded but will be discarded during next level saving. This class is not longer being used"), *GetName());
}
