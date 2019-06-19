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
	, bIsVisible(true)
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

void ALandscapeBlueprintCustomBrush::RequestLandscapeUpdate()
{
#if WITH_EDITORONLY_DATA
	if (OwningLandscape)
	{
		OwningLandscape->RequestLayersContentUpdateForceAll();
	}
#endif
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

void ALandscapeBlueprintCustomBrush::SetIsVisible(bool bInIsVisible)
{
	Modify();
	bIsVisible = bInIsVisible;
	GetOwningLandscape()->OnBPCustomBrushChanged();
}

void ALandscapeBlueprintCustomBrush::SetAffectsHeightmap(bool bInAffectsHeightmap)
{
	Modify();
	AffectHeightmap = bInAffectsHeightmap;
	GetOwningLandscape()->OnBPCustomBrushChanged();
}

void ALandscapeBlueprintCustomBrush::SetAffectsWeightmap(bool bInAffectsWeightmap)
{
	Modify();
	AffectWeightmap = bInAffectsWeightmap;
	GetOwningLandscape()->OnBPCustomBrushChanged();
}

void ALandscapeBlueprintCustomBrush::SetOwningLandscape(ALandscape* InOwningLandscape)
{
	Modify();
	OwningLandscape = InOwningLandscape;
}

ALandscape* ALandscapeBlueprintCustomBrush::GetOwningLandscape() const
{ 
	return OwningLandscape; 
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
		OwningLandscape->OnBPCustomBrushChanged();
	}
}

void ALandscapeBlueprintCustomBrush::Destroyed()
{
	Super::Destroyed();

	if (OwningLandscape && !GIsReinstancing)
	{
		OwningLandscape->RemoveBrush(this);
	}
	OwningLandscape = nullptr;
}

#endif

#undef LOCTEXT_NAMESPACE
