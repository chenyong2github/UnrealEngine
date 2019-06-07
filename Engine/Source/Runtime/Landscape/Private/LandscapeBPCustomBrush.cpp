// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LandscapeBPCustomBrush.h"
#include "CoreMinimal.h"
#include "LandscapeProxy.h"
#include "Landscape.h"

#define LOCTEXT_NAMESPACE "Landscape"

ALandscapeBlueprintCustomBrush::ALandscapeBlueprintCustomBrush(const FObjectInitializer& ObjectInitializer)
#if WITH_EDITORONLY_DATA
	: OwningLandscape(nullptr)
	, bIsCommited(false)
	, bIsInitialized(false)
#endif
{
	USceneComponent* SceneComp = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
	RootComponent = SceneComp;

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_DuringPhysics;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.SetTickFunctionEnable(true);
	bIsEditorOnlyActor = true;
}

void ALandscapeBlueprintCustomBrush::Tick(float DeltaSeconds)
{
	// Forward the Tick to the instances class of this BP
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint))
	{
		TGuardValue<bool> AutoRestore(GAllowActorScriptExecutionInEditor, true);
		ReceiveTick(DeltaSeconds);
	}

	Super::Tick(DeltaSeconds);
}

bool ALandscapeBlueprintCustomBrush::ShouldTickIfViewportsOnly() const
{
	return true;
}

#if WITH_EDITOR

void ALandscapeBlueprintCustomBrush::SetCommitState(bool InCommited)
{
#if WITH_EDITORONLY_DATA
	bListedInSceneOutliner = !InCommited;
	bEditable = !InCommited;
	bIsCommited = InCommited;
#endif
}

void ALandscapeBlueprintCustomBrush::SetOwningLandscape(ALandscape* InOwningLandscape)
{
	OwningLandscape = InOwningLandscape;
}

ALandscape* ALandscapeBlueprintCustomBrush::GetOwningLandscape() const
{ 
	return OwningLandscape; 
}

void ALandscapeBlueprintCustomBrush::SetIsInitialized(bool InIsInitialized)
{
	bIsInitialized = InIsInitialized;
}

bool ALandscapeBlueprintCustomBrush::IsAffectingWeightmapLayer(const FName& InLayerName) const
{
	return AffectedWeightmapLayers.Contains(InLayerName);
}

void ALandscapeBlueprintCustomBrush::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	if (OwningLandscape != nullptr)
	{
		if (bFinished)
		{
			OwningLandscape->RequestLayersContentUpdateForceAll();
		}
		else
		{
			OwningLandscape->RequestLayersContentUpdate(ELandscapeLayerUpdateMode::Update_All_Editing);
		}
	}
}

void ALandscapeBlueprintCustomBrush::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	
	if (OwningLandscape)
	{
		const FName PropertyName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;
		if (PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeBlueprintCustomBrush, AffectHeightmap) || 
			PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeBlueprintCustomBrush, AffectWeightmap))
		{
			// Should trigger a rebuild of the UI so the visual is updated with changes made to actor
			//TODO: find a way to trigger the update of the UI
			//FEdModeLandscape* EdMode = (FEdModeLandscape*)GLevelEditorModeTools().GetActiveMode(FBuiltinEditorModes::EM_Landscape);
			//EdMode->RefreshDetailPanel();
			OwningLandscape->OnBPCustomBrushChanged(); 
		}
	}
}
#endif

ALandscapeBlueprintCustomSimulationBrush::ALandscapeBlueprintCustomSimulationBrush(const FObjectInitializer& ObjectInitializer)
{
}

#undef LOCTEXT_NAMESPACE
