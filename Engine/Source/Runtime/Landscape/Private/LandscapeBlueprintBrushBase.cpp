// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LandscapeBlueprintBrushBase.h"
#include "CoreMinimal.h"
#include "LandscapeProxy.h"
#include "Landscape.h"

#define LOCTEXT_NAMESPACE "Landscape"

ALandscapeBlueprintBrushBase::ALandscapeBlueprintBrushBase(const FObjectInitializer& ObjectInitializer)
#if WITH_EDITORONLY_DATA
	: OwningLandscape(nullptr)
	, AffectHeightmap(false)
	, AffectWeightmap(false)
	, bIsVisible(true)
	, LastRequestLayersContentUpdateFrameNumber(0)
#endif
{
#if WITH_EDITOR
	USceneComponent* SceneComp = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
	RootComponent = SceneComp;

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_DuringPhysics;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.SetTickFunctionEnable(true);
	bIsEditorOnlyActor = true;
#endif // WITH_EDITOR
}

void ALandscapeBlueprintBrushBase::RequestLandscapeUpdate()
{
#if WITH_EDITORONLY_DATA
	if (OwningLandscape)
	{
		uint32 ModeMask = 0;
		if (AffectHeightmap)
		{
			ModeMask |= ELandscapeLayerUpdateMode::Update_Heightmap_Editing_NoCollision;
		}
		if (AffectWeightmap)
		{
			ModeMask |= ELandscapeLayerUpdateMode::Update_Weightmap_Editing_NoCollision;
		}
		if (ModeMask)
		{
			OwningLandscape->RequestLayersContentUpdateForceAll((ELandscapeLayerUpdateMode)ModeMask);
			LastRequestLayersContentUpdateFrameNumber = GFrameNumber;
		}
	}
#endif
}

#if WITH_EDITOR
void ALandscapeBlueprintBrushBase::Tick(float DeltaSeconds)
{
#if WITH_EDITORONLY_DATA
	// Avoid computing collision and client updates every frame
	// Wait until we didn't trigger any more landscape update requests (padding of a couple of frames)
	if (LastRequestLayersContentUpdateFrameNumber + 5 == GFrameNumber)
	{
		uint32 ModeMask = 0;
		if (AffectHeightmap)
		{
			ModeMask |= ELandscapeLayerUpdateMode::Update_Heightmap_All;
		}
		if (AffectWeightmap)
		{
			ModeMask |= ELandscapeLayerUpdateMode::Update_Weightmap_All;
		}
		if (ModeMask)
		{
			OwningLandscape->RequestLayersContentUpdateForceAll((ELandscapeLayerUpdateMode)ModeMask);
		}
	}
#endif

	// Forward the Tick to the instances class of this BP
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint))
	{
		TGuardValue<bool> AutoRestore(GAllowActorScriptExecutionInEditor, true);
		ReceiveTick(DeltaSeconds);
	}

	Super::Tick(DeltaSeconds);
}

bool ALandscapeBlueprintBrushBase::ShouldTickIfViewportsOnly() const
{
	return true;
}

void ALandscapeBlueprintBrushBase::SetIsVisible(bool bInIsVisible)
{
#if WITH_EDITORONLY_DATA
	Modify();
	bIsVisible = bInIsVisible;
	if (OwningLandscape)
	{
		OwningLandscape->OnBlueprintBrushChanged();
	}
#endif
}

void ALandscapeBlueprintBrushBase::SetAffectsHeightmap(bool bInAffectsHeightmap)
{
#if WITH_EDITORONLY_DATA
	Modify();
	AffectHeightmap = bInAffectsHeightmap;
	if (OwningLandscape)
	{
		OwningLandscape->OnBlueprintBrushChanged();
	}
#endif
}

void ALandscapeBlueprintBrushBase::SetAffectsWeightmap(bool bInAffectsWeightmap)
{
#if WITH_EDITORONLY_DATA
	Modify();
	AffectWeightmap = bInAffectsWeightmap;
	if (OwningLandscape)
	{
		OwningLandscape->OnBlueprintBrushChanged();
	}
#endif
}

bool ALandscapeBlueprintBrushBase::IsAffectingWeightmapLayer(const FName& InLayerName) const
{
#if WITH_EDITORONLY_DATA
	return AffectedWeightmapLayers.Contains(InLayerName);
#else
	return false;
#endif
}

void ALandscapeBlueprintBrushBase::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);
#if WITH_EDITORONLY_DATA
	RequestLandscapeUpdate();
#endif
}

void ALandscapeBlueprintBrushBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
#if WITH_EDITORONLY_DATA
	if (OwningLandscape)
	{
		OwningLandscape->OnBlueprintBrushChanged();
	}
#endif
}

void ALandscapeBlueprintBrushBase::Destroyed()
{
	Super::Destroyed();
#if WITH_EDITORONLY_DATA
	if (OwningLandscape && !GIsReinstancing)
	{
		OwningLandscape->RemoveBrush(this);
	}
	OwningLandscape = nullptr;
#endif
}

void ALandscapeBlueprintBrushBase::SetOwningLandscape(ALandscape* InOwningLandscape)
{
#if WITH_EDITORONLY_DATA
	if (OwningLandscape == InOwningLandscape)
	{
		return;
	}

	const bool bAlwaysMarkDirty = false;
	Modify(bAlwaysMarkDirty);

	if (OwningLandscape)
	{
		OwningLandscape->OnBlueprintBrushChanged();
	}

	OwningLandscape = InOwningLandscape;

	if (OwningLandscape)
	{
		OwningLandscape->OnBlueprintBrushChanged();
	}
#endif
}

ALandscape* ALandscapeBlueprintBrushBase::GetOwningLandscape() const
{
#if WITH_EDITORONLY_DATA
	return OwningLandscape;
#else
	return nullptr;
#endif
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE