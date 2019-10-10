// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "VPScoutingSubsystem.h"
#include "VPUtilitiesEditorModule.h"

#include "IVREditorModule.h"
#include "VREditorMode.h"
#include "UI/VREditorUISystem.h"
#include "VREditorStyle.h"
#include "WidgetBlueprint.h"
#include "EditorUtilityActor.h"
#include "EditorUtilityWidget.h"
#include "IVREditorModule.h"
#include "UObject/ConstructorHelpers.h"
#include "VPSettings.h"
#include "VPUtilitiesEditorSettings.h"
#include "LevelEditor.h"

const FName UVPScoutingSubsystem::VProdPanelID = FName(TEXT("VirtualProductionPanel"));
const FName UVPScoutingSubsystem::VProdPanelLeftID = FName(TEXT("VirtualProductionPanelLeft"));
const FName UVPScoutingSubsystem::VProdPanelRightID = FName(TEXT("VirtualProductionPanelRight"));
const FName UVPScoutingSubsystem::VProdPanelContextID = FName(TEXT("VirtualProductionPanelContext"));
const FName UVPScoutingSubsystem::VProdPanelTimelineID = FName(TEXT("VirtualProductionPanelTimeline"));
const FName UVPScoutingSubsystem::VProdPanelMeasureID = FName(TEXT("VirtualProductionPanelMeasure"));
const FName UVPScoutingSubsystem::VProdPanelGafferID = FName(TEXT("VirtualProductionPanelGaffer"));

UVPScoutingSubsystem::UVPScoutingSubsystem()
	: UEditorSubsystem()
{
}

void UVPScoutingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	UE_LOG(LogVPUtilitiesEditor, Log, TEXT("VP Scouting subsystem initialized."));

	// Setup VR editor settings from user preferences
	UVPUtilitiesEditorSettings* VPUtilitiesEditorSettings = GetMutableDefault<UVPUtilitiesEditorSettings>();
	
	// Turn on/off transform VR gizmo
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("VI.ShowTransformGizmo"));
	CVar->Set(VPUtilitiesEditorSettings->bUseTransformGizmo);

	//Initialize drag scale from saved config file
	GripNavSpeedCoeff = 4.0f;
	CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("VI.DragScale"));
	CVar->Set(VPUtilitiesEditorSettings->GripNavSpeed * GripNavSpeedCoeff);

	//Turn on/off grip nav inertia
	CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("VI.HighSpeedInertiaDamping"));
	if (VPUtilitiesEditorSettings->bUseGripInertiaDamping)
	{
		CVar->Set(VPUtilitiesEditorSettings->InertiaDamping);
	}
	else
	{
		CVar->Set(0);
	}

	// to do final initializations at the right time
	EngineInitCompleteDelegate = FCoreDelegates::OnFEngineLoopInitComplete.AddUObject(this, &UVPScoutingSubsystem::OnEngineInitComplete);
}

void UVPScoutingSubsystem::Deinitialize()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
	LevelEditorModule.OnMapChanged().RemoveAll(this);
}

void UVPScoutingSubsystem::OnEngineInitComplete()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
	LevelEditorModule.OnMapChanged().AddUObject(this, &UVPScoutingSubsystem::OnMapChanged);

	FCoreDelegates::OnFEngineLoopInitComplete.Remove(EngineInitCompleteDelegate);
	EngineInitCompleteDelegate.Reset();
}

void UVPScoutingSubsystem::OnMapChanged(UWorld * World, EMapChangeType MapChangeType)
{
	if (MapChangeType == EMapChangeType::TearDownWorld)
	{
		VProdHelper = nullptr;
	}
	else if (MapChangeType == EMapChangeType::LoadMap || MapChangeType == EMapChangeType::NewMap)
	{
		const UVPUtilitiesEditorSettings* VPUtilitiesEditorSettings = GetDefault<UVPUtilitiesEditorSettings>();
		const FString ClassPath = VPUtilitiesEditorSettings->ScoutingSubsystemEdititorUtilityActorClassPath.ToString();
		UClass* EditorUtilityActorClass = LoadObject<UClass>(nullptr, *ClassPath);

		if (EditorUtilityActorClass)
		{
			VProdHelper = NewObject<AEditorUtilityActor>(GetTransientPackage(), EditorUtilityActorClass);
		}
		else
		{
			UE_LOG(LogVPUtilitiesEditor, Warning, TEXT("Failed loading EditorUtilityActorClass \"%s\""), *ClassPath);
		}
	}
}

void UVPScoutingSubsystem::ToggleVRScoutingUI(FVREditorFloatingUICreationContext& CreationContext)
{	
	// @todo: Add lookup like like bool UVREditorUISystem::EditorUIPanelExists(const VREditorPanelID& InPanelID) const
	// Return if users try to create a panel that already exists
		
	if (CreationContext.WidgetClass == nullptr || CreationContext.PanelID == TEXT(""))
	{
		UE_LOG(LogVPUtilitiesEditor, Warning, TEXT("UVPScoutingSubsystem::ToggleVRScoutingUI - WidgetClass or PanelID can't be null."));
		return; // @todo: Remove early rejection code, hook up UVPSettings::VirtualScoutingUI instead
	}
	
	// Account for actors trying to call this function from their destructor when VR mode ends (UI system is one of the earliest systems getting shut down)	
	UVREditorMode const* const VRMode = IVREditorModule::Get().GetVRMode();
	if (VRMode && VRMode->UISystemIsActive())
	{
		bool bPanelVisible = VRMode->GetUISystem().IsShowingEditorUIPanel(CreationContext.PanelID);

		// Close panel if currently visible
		if (bPanelVisible)
		{
			// Close the existing panel by passing null as the widget. We don't care about any of the other parameters in this case
			CreationContext.WidgetClass = nullptr;
			CreationContext.PanelSize = FVector2D(1, 1); // Guard against 0,0 user input. The actual size is not important when closing a panel, but a check() would trigger
			IVREditorModule::Get().UpdateExternalUMGUI(CreationContext);
		}
		else // Otherwise open a new one - with the user-defined VProd UI being the default
		{
			// @todo: Currently won't ever be true
			if (CreationContext.WidgetClass == nullptr)
			{
				const TSoftClassPtr<UEditorUtilityWidget> WidgetClass = GetDefault<UVPUtilitiesEditorSettings>()->VirtualScoutingUI;
				WidgetClass.LoadSynchronous();
				if (WidgetClass.IsValid())
				{
					CreationContext.WidgetClass = WidgetClass.Get();
				}
			}

			if (CreationContext.WidgetClass != nullptr)
			{
				IVREditorModule::Get().UpdateExternalUMGUI(CreationContext);
			}
			else
			{
				UE_LOG(LogVPUtilitiesEditor, Warning, TEXT("UVPScoutingSubsystem::ToggleVRScoutingUI - Failed to open widget-based VR window."));
			}
		}
	}
}

void UVPScoutingSubsystem::HideInfoDisplayPanel()
{
	UVREditorMode*  VRMode = IVREditorModule::Get().GetVRMode();
	if (VRMode && VRMode->UISystemIsActive())
	{
		UVREditorUISystem& UISystem = VRMode->GetUISystem();
		AVREditorFloatingUI* Panel = UISystem.GetPanel(UVREditorUISystem::InfoDisplayPanelID);
		if (Panel->IsUIVisible()) 
		{
			Panel->ShowUI(false);
		}
	}
}

bool UVPScoutingSubsystem::IsVRScoutingUIOpen(const FName& PanelID)
{
	return IVREditorModule::Get().GetVRMode()->GetUISystem().IsShowingEditorUIPanel(PanelID);
}

AVREditorFloatingUI * UVPScoutingSubsystem::GetPanelActor(const FName& PanelID) const
{
	return IVREditorModule::Get().GetVRMode()->GetUISystem().GetPanel(PanelID);
	
}

UUserWidget * UVPScoutingSubsystem::GetPanelWidget(const FName & PanelID) const
{
	AVREditorFloatingUI* Panel = GetPanelActor(PanelID);
	if (Panel == nullptr)
	{
		return nullptr;
	}
	else
	{
		return Panel->GetUserWidget();
	}
}

TArray<UVREditorInteractor*> UVPScoutingSubsystem::GetActiveEditorVRControllers()
{
	IVREditorModule& VREditorModule = IVREditorModule::Get();
	UVREditorMode* VRMode = VREditorModule.GetVRMode();
	
	const TArray<UVREditorInteractor*> Interactors = VRMode->GetVRInteractors();
	ensureMsgf(Interactors.Num() == 2, TEXT("Expected 2 VR controllers from VREditorMode, got %d"), Interactors.Num());
	return Interactors;		
}


FString UVPScoutingSubsystem::GetDirectorName()
{
	FString DirectorName = GetDefault<UVPSettings>()->DirectorName;
	if (DirectorName == TEXT(""))
	{
		DirectorName = "Undefined";
	}
	return DirectorName;
}

FString UVPScoutingSubsystem::GetShowName()
{
	FString ShowName = GetDefault<UVPSettings>()->ShowName;
	if (ShowName == TEXT(""))
	{
		ShowName = "Undefined";
	}
	return ShowName;
}

bool UVPScoutingSubsystem::IsUsingMetricSystem()
{
	return GetDefault<UVPUtilitiesEditorSettings>()->bUseMetric;
}

void UVPScoutingSubsystem::SetIsUsingMetricSystem(const bool bInUseMetricSystem)
{
	UVPUtilitiesEditorSettings* VPUtilitiesEditorSettings = GetMutableDefault<UVPUtilitiesEditorSettings>();
	VPUtilitiesEditorSettings->bUseMetric = bInUseMetricSystem;
	VPUtilitiesEditorSettings->SaveConfig();
}

bool UVPScoutingSubsystem::IsUsingTransformGizmo()
{
	return GetDefault<UVPUtilitiesEditorSettings>()->bUseTransformGizmo;
}

void UVPScoutingSubsystem::SetIsUsingTransformGizmo(const bool bInIsUsingTransformGizmo)
{
	UVPUtilitiesEditorSettings* VPUtilitiesEditorSettings = GetMutableDefault<UVPUtilitiesEditorSettings>();
	if (bInIsUsingTransformGizmo != VPUtilitiesEditorSettings->bUseTransformGizmo)
	{
		VPUtilitiesEditorSettings->bUseTransformGizmo = bInIsUsingTransformGizmo;
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("VI.ShowTransformGizmo"));
		CVar->Set(bInIsUsingTransformGizmo);
		VPUtilitiesEditorSettings->SaveConfig();
	}
}

float UVPScoutingSubsystem::GetFlightSpeed()
{
	return GetDefault<UVPUtilitiesEditorSettings>()->FlightSpeed;
}

void UVPScoutingSubsystem::SetFlightSpeed(const float InFlightSpeed)
{
	UVPUtilitiesEditorSettings* VPUtilitiesEditorSettings = GetMutableDefault<UVPUtilitiesEditorSettings>();
	VPUtilitiesEditorSettings->FlightSpeed = InFlightSpeed;
	VPUtilitiesEditorSettings->SaveConfig();
}

float UVPScoutingSubsystem::GetGripNavSpeed()
{
	return GetDefault<UVPUtilitiesEditorSettings>()->GripNavSpeed;
}

void UVPScoutingSubsystem::SetGripNavSpeed(const float InGripNavSpeed)
{
	UVPUtilitiesEditorSettings* VPUtilitiesEditorSettings = GetMutableDefault<UVPUtilitiesEditorSettings>();
	VPUtilitiesEditorSettings->GripNavSpeed = InGripNavSpeed;
	VPUtilitiesEditorSettings->SaveConfig();
}

bool UVPScoutingSubsystem::IsUsingInertiaDamping()
{
	return GetDefault<UVPUtilitiesEditorSettings>()->bUseGripInertiaDamping;
}

void UVPScoutingSubsystem::SetIsUsingInertiaDamping(const bool bInIsUsingInertiaDamping)
{
	//Save this value in editor settings and set the console variable which is used for inertia damping
	UVPUtilitiesEditorSettings* VPUtilitiesEditorSettings = GetMutableDefault<UVPUtilitiesEditorSettings>();
	VPUtilitiesEditorSettings->bUseGripInertiaDamping = bInIsUsingInertiaDamping;
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("VI.HighSpeedInertiaDamping"));
	if (bInIsUsingInertiaDamping)
	{
		CVar->Set(VPUtilitiesEditorSettings->InertiaDamping);
	}
	else
	{
		CVar->Set(0);
	}
	VPUtilitiesEditorSettings->SaveConfig();
}

bool UVPScoutingSubsystem::IsHelperSystemEnabled()
{
	return GetDefault<UVPUtilitiesEditorSettings>()->bIsHelperSystemEnabled;
}

void UVPScoutingSubsystem::SetIsHelperSystemEnabled(const bool bInIsHelperSystemEnabled)
{
	UVPUtilitiesEditorSettings* VPUtilitiesEditorSettings = GetMutableDefault<UVPUtilitiesEditorSettings>();
	VPUtilitiesEditorSettings->bIsHelperSystemEnabled = bInIsHelperSystemEnabled;
	VPUtilitiesEditorSettings->SaveConfig();
}
