// Copyright Epic Games, Inc. All Rights Reserved.


#include "CoreMinimal.h"
#include "Preferences/CascadeOptions.h"
#include "Preferences/CurveEdOptions.h"
#include "Preferences/MaterialEditorOptions.h"
#include "Preferences/PersonaOptions.h"
#include "Preferences/AnimationBlueprintEditorOptions.h"
#include "Preferences/PhysicsAssetEditorOptions.h"
#include "Preferences/MaterialStatsOptions.h"
#include "FrameNumberDisplayFormat.h"

// @todo find a better place for all of this, preferably in the appropriate modules
// though this would require the classes to be relocated as well

//
// UCascadeOptions
// 
UCascadeOptions::UCascadeOptions(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////
////////////////// UPhysicsAssetEditorOptions /////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
UPhysicsAssetEditorOptions::UPhysicsAssetEditorOptions(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PhysicsBlend = 1.0f;
	bUpdateJointsFromAnimation = false;
	MaxFPS = -1;

	// These should duplicate defaults from UPhysicsHandleComponent
	HandleLinearDamping = 200.0f;
	HandleLinearStiffness = 750.0f;
	HandleAngularDamping = 500.0f;
	HandleAngularStiffness = 1500.0f;
	InterpolationSpeed = 50.f;

	bShowConstraintsAsPoints = false;
	ConstraintDrawSize = 1.0f;

	// view options
	MeshViewMode = EPhysicsAssetEditorRenderMode::Solid;
	CollisionViewMode = EPhysicsAssetEditorRenderMode::Solid;
	ConstraintViewMode = EPhysicsAssetEditorConstraintViewMode::AllLimits;
	SimulationMeshViewMode = EPhysicsAssetEditorRenderMode::Solid;
	SimulationCollisionViewMode = EPhysicsAssetEditorRenderMode::Solid;
	SimulationConstraintViewMode = EPhysicsAssetEditorConstraintViewMode::None;

	CollisionOpacity = 0.3f;
	bSolidRenderingForSelectedOnly = false;
	bHideSimulatedBodies = false;
	bHideKinematicBodies = false;
	bResetClothWhenSimulating = false;
}

UMaterialEditorOptions::UMaterialEditorOptions(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UMaterialStatsOptions::UMaterialStatsOptions(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if PLATFORM_WINDOWS
	//#todo-sm6
	bPlatformUsed[/*GMaxRHIFeatureLevel == ERHIFeatureLevel::SM5 ? */SP_PCD3D_SM5/* : SP_PCD3D_SM4*/] = 1;
#elif PLATFORM_IOS
	bPlatformUsed[SP_METAL] = 1;
#endif

	bMaterialQualityUsed[EMaterialQualityLevel::High] = 1;
}

UAnimationBlueprintEditorOptions::UAnimationBlueprintEditorOptions(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UCurveEdOptions::UCurveEdOptions(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void FViewportConfigOptions::SetToDefault()
{
	ViewModeIndex = VMI_Lit;
	ViewFOV = 53.43f;
	CameraSpeedSetting = 4;
	CameraSpeedScalar = 1.0f;
	CameraFollowMode = EAnimationViewportCameraFollowMode::None;
	CameraFollowBoneName = NAME_None;
}

void FAssetEditorOptions::SetViewportConfigsToDefault()
{
	for (FViewportConfigOptions& ViewportConfig : ViewportConfigs)
	{
		ViewportConfig.SetToDefault();
	}
}

UPersonaOptions::UPersonaOptions(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, DefaultLocalAxesSelection(2)
	, DefaultBoneDrawSelection(1)
	, bAllowPreviewMeshCollectionsToSelectFromDifferentSkeletons(true)
{
	AssetEditorOptions.AddUnique(FAssetEditorOptions(TEXT("SkeletonEditor")));
	AssetEditorOptions.AddUnique(FAssetEditorOptions(TEXT("SkeletalMeshEditor")));
	AssetEditorOptions.AddUnique(FAssetEditorOptions(TEXT("AnimationEditor")));
	AssetEditorOptions.AddUnique(FAssetEditorOptions(TEXT("AnimationBlueprintEditor")));
	AssetEditorOptions.AddUnique(FAssetEditorOptions(TEXT("PhysicsAssetEditor")));

	for(FAssetEditorOptions& EditorOptions : AssetEditorOptions)
	{
		EditorOptions.SetViewportConfigsToDefault();
	}

	SectionTimingNodeColor = FLinearColor(0.39f, 0.39f, 1.0f, 0.75f);
	NotifyTimingNodeColor = FLinearColor(0.8f, 0.1f, 0.1f);
	BranchingPointTimingNodeColor = FLinearColor(0.5f, 1.0f, 1.0f);

	bAutoAlignFloorToMesh = true;

	NumFolderFiltersInAssetBrowser = 2;

	bUseAudioAttenuation = true;

	CurveEditorSnapInterval = 0.01f;

	// Default to millisecond resolution
	TimelineScrubSnapValue = 1000;

	TimelineDisplayFormat = EFrameNumberDisplayFormats::Frames;

	bTimelineDisplayPercentage = true;
	bTimelineDisplayFormatSecondary = true;

	bTimelineDisplayCurveKeys = false;

	TimelineEnabledSnaps = { "CompositeSegment", "MontageSection" };
}

void UPersonaOptions::SetShowGrid( bool bInShowGrid )
{
	bShowGrid = bInShowGrid;
	SaveConfig();
}

void UPersonaOptions::SetHighlightOrigin( bool bInHighlightOrigin )
{
	bHighlightOrigin = bInHighlightOrigin;
	SaveConfig();
}

void UPersonaOptions::SetViewModeIndex( FName InContext, EViewModeIndex InViewModeIndex, int32 InViewportIndex )
{
	check(InViewportIndex >= 0 && InViewportIndex < 4);

	FAssetEditorOptions& Options = GetAssetEditorOptions(InContext);
	Options.ViewportConfigs[InViewportIndex].ViewModeIndex = InViewModeIndex;
	SaveConfig();
}

void UPersonaOptions::SetAutoAlignFloorToMesh(bool bInAutoAlignFloorToMesh)
{
	bAutoAlignFloorToMesh = bInAutoAlignFloorToMesh;
	SaveConfig();
}

void UPersonaOptions::SetMuteAudio( bool bInMuteAudio )
{
	bMuteAudio = bInMuteAudio;
	SaveConfig();
}

void UPersonaOptions::SetUseAudioAttenuation( bool bInUseAudioAttenuation )
{
	bUseAudioAttenuation = bInUseAudioAttenuation;
	SaveConfig();
}

void UPersonaOptions::SetViewFOV( FName InContext, float InViewFOV, int32 InViewportIndex )
{
	check(InViewportIndex >= 0 && InViewportIndex < 4);

	FAssetEditorOptions& Options = GetAssetEditorOptions(InContext);
	Options.ViewportConfigs[InViewportIndex].ViewFOV = InViewFOV;
	SaveConfig();
}

void UPersonaOptions::SetCameraSpeed(FName InContext, int32 InCameraSpeed, int32 InViewportIndex)
{
	check(InViewportIndex >= 0 && InViewportIndex < 4);

	FAssetEditorOptions& Options = GetAssetEditorOptions(InContext);
	Options.ViewportConfigs[InViewportIndex].CameraSpeedSetting = InCameraSpeed;
	SaveConfig();
}

void UPersonaOptions::SetCameraSpeedScalar(FName InContext, float InCameraSpeedScalar, int32 InViewportIndex)
{
	check(InViewportIndex >= 0 && InViewportIndex < 4);

	FAssetEditorOptions& Options = GetAssetEditorOptions(InContext);
	Options.ViewportConfigs[InViewportIndex].CameraSpeedScalar = InCameraSpeedScalar;
	SaveConfig();
}

void UPersonaOptions::SetViewCameraFollow( FName InContext, EAnimationViewportCameraFollowMode InCameraFollowMode, FName InCameraFollowBoneName, int32 InViewportIndex )
{
	check(InViewportIndex >= 0 && InViewportIndex < 4);

	FAssetEditorOptions& Options = GetAssetEditorOptions(InContext);
	Options.ViewportConfigs[InViewportIndex].CameraFollowMode = InCameraFollowMode;
	Options.ViewportConfigs[InViewportIndex].CameraFollowBoneName = InCameraFollowBoneName;
	SaveConfig();
}

void UPersonaOptions::SetDefaultLocalAxesSelection( uint32 InDefaultLocalAxesSelection )
{
	DefaultLocalAxesSelection = InDefaultLocalAxesSelection;
	SaveConfig();
}

void UPersonaOptions::SetDefaultBoneDrawSelection(uint32 InDefaultBoneDrawSelection)
{
	DefaultBoneDrawSelection = InDefaultBoneDrawSelection;
	SaveConfig();
}

void UPersonaOptions::SetShowMeshStats( int32 InShowMeshStats )
{
	ShowMeshStats = InShowMeshStats;
	SaveConfig();
}


void UPersonaOptions::SetSectionTimingNodeColor(const FLinearColor& InColor)
{
	SectionTimingNodeColor = InColor;
	SaveConfig();
}

void UPersonaOptions::SetNotifyTimingNodeColor(const FLinearColor& InColor)
{
	NotifyTimingNodeColor = InColor;
	SaveConfig();
}

void UPersonaOptions::SetBranchingPointTimingNodeColor(const FLinearColor& InColor)
{
	BranchingPointTimingNodeColor = InColor;
	SaveConfig();
}

FAssetEditorOptions& UPersonaOptions::GetAssetEditorOptions(const FName& InContext)
{
	FAssetEditorOptions* FoundOptions = AssetEditorOptions.FindByPredicate([InContext](const FAssetEditorOptions& InOptions)
	{
		return InOptions.Context == InContext;
	});

	if(!FoundOptions)
	{
		AssetEditorOptions.Add(FAssetEditorOptions(InContext));
		return AssetEditorOptions.Last();
	}
	else
	{
		return *FoundOptions;
	}
}