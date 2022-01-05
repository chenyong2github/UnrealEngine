// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelInstance/LevelInstanceEditorPivotActor.h"
#include "LevelInstance/LevelInstanceActor.h"
#include "LevelInstance/LevelInstanceSubsystem.h"
#include "GameFramework/WorldSettings.h"
#include "LevelUtils.h"
#include "Engine/LevelStreaming.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

ALevelInstancePivot::ALevelInstancePivot(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
	RootComponent->Mobility = EComponentMobility::Static;
#if WITH_EDITORONLY_DATA
	bActorLabelEditable = false;
#endif // WITH_EDITORONLY_DATA
}

#if WITH_EDITOR

ALevelInstancePivot* ALevelInstancePivot::Create(ALevelInstance* LevelInstanceActor, ULevelStreaming* LevelStreaming)
{
	FActorSpawnParameters SpawnParams;
	SpawnParams.OverrideLevel = LevelStreaming->GetLoadedLevel();
	SpawnParams.bCreateActorPackage = false;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bNoFail = true;
		
	// We place the pivot actor at the LevelInstance Transform so that it makes sense to the user (the pivot being the zero)
	ALevelInstancePivot* PivotActor = LevelInstanceActor->GetWorld()->SpawnActor<ALevelInstancePivot>(LevelInstanceActor->GetActorLocation(), LevelInstanceActor->GetActorRotation(), SpawnParams);
	
	AWorldSettings* WorldSettings = LevelStreaming->GetLoadedLevel()->GetWorldSettings();

	
	// Keep Spawn World Transform in case Level Instance transform changes.
	PivotActor->SpawnTransform = PivotActor->GetActorTransform();
	PivotActor->OriginalPivotOffset = WorldSettings->LevelInstancePivotOffset;
	PivotActor->SetLevelInstanceID(LevelInstanceActor->GetLevelInstanceID());
	// Set Label last as this will call PostEditChangeProperty and update the offset so other fields need to be setup first.
	PivotActor->SetActorLabel(TEXT("Pivot"));

	return PivotActor;
}

void ALevelInstancePivot::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	if (bFinished && !FLevelUtils::IsApplyingLevelTransform())
	{
		UpdateOffset();
	}
}

void ALevelInstancePivot::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	UpdateOffset();
}

void ALevelInstancePivot::PostEditUndo()
{
	Super::PostEditUndo();

	UpdateOffset();
}

void ALevelInstancePivot::SetPivot(ELevelInstancePivotType PivotType, AActor* PivotActor)
{
	check(PivotType != ELevelInstancePivotType::Actor || PivotActor != nullptr);

	Modify();
	if (PivotType == ELevelInstancePivotType::Actor)
	{
		SetActorLocation(PivotActor->GetActorLocation());
	}
	else if(PivotType == ELevelInstancePivotType::Center || PivotType == ELevelInstancePivotType::CenterMinZ)
	{
		ULevelInstanceSubsystem* LevelInstanceSubsystem = UWorld::GetSubsystem<ULevelInstanceSubsystem>(GetWorld());
		ALevelInstance* LevelInstance = LevelInstanceSubsystem->GetLevelInstance(GetLevelInstanceID());
		FBox OutBounds;
		LevelInstanceSubsystem->GetLevelInstanceBounds(LevelInstance, OutBounds);
		FVector Location = OutBounds.GetCenter();
		if (PivotType == ELevelInstancePivotType::CenterMinZ)
		{
			Location.Z = OutBounds.Min.Z;
		}
		SetActorLocation(Location);
	}
	else if (PivotType == ELevelInstancePivotType::WorldOrigin)
	{
		SetActorLocation(FVector(0.f, 0.f, 0.f));
	}
	else // unsupported
	{
		check(0);
	}

	// Update gizmo
	if (GEditor)
	{
		GEditor->NoteSelectionChange();
	}

	UpdateOffset();
}

void ALevelInstancePivot::UpdateOffset()
{
	// Offset Change is the relative transform of the pivot actor to its spawn transform (we only care about relative translation as we don't support rotation on pivot)
	FVector LocalToPivot = GetActorTransform().GetRelativeTransform(SpawnTransform).GetTranslation();

	// We then apply that offset to the original pivot
	FVector NewPivotOffset = OriginalPivotOffset - LocalToPivot;

	AWorldSettings* WorldSettings = GetLevel()->GetWorldSettings();
	if (!NewPivotOffset.Equals(WorldSettings->LevelInstancePivotOffset))
	{
		WorldSettings->Modify();
		WorldSettings->LevelInstancePivotOffset = NewPivotOffset;
	}
}

#endif
