// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "VPScoutingSubsystem.h"
#include "VPUtilitiesEditorModule.h"

#include "IVREditorModule.h"
#include "VREditorMode.h"
#include "UI/VREditorUISystem.h"
#include "VREditorStyle.h"
#include "WidgetBlueprint.h"
#include "EditorUtilityActor.h"
#include "IVREditorModule.h"
#include "UObject/ConstructorHelpers.h"
#include "VPSettings.h"
#include "VPUtilitiesEditorSettings.h"

const FName UVPScoutingSubsystem::VProdPanelID = FName(TEXT("VirtualProductionPanel"));
const FName UVPScoutingSubsystem::VProdPanelLeftID = FName(TEXT("VirtualProductionPanelLeft"));
const FName UVPScoutingSubsystem::VProdPanelRightID = FName(TEXT("VirtualProductionPanelRight"));

UVPScoutingSubsystem::UVPScoutingSubsystem()
	: UEditorSubsystem()
{
	static ConstructorHelpers::FClassFinder<AEditorUtilityActor> EditorUtilityActorClassFinder(TEXT("/VirtualProductionUtilities/VirtualProductionHelpers"));
	EditorUtilityActorClass = EditorUtilityActorClassFinder.Class;
}

void UVPScoutingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	UE_LOG(LogVPUtilitiesEditor, Log, TEXT("VP Scouting subsystem initialized."));

	if (EditorUtilityActorClass)
	{
		VProdHelper = NewObject<AEditorUtilityActor>(GetTransientPackage(), EditorUtilityActorClass);
	}

	// Init config settings and cvar to get the VR editor into the state we consider best for VR scouting:

	// Turn off transform VR gizmo, we want to transform all objects freely
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("VI.ShowTransformGizmo"));
	CVar->Set(0);	
	FlightSpeedCoeff = 0.5f;
}

void UVPScoutingSubsystem::Deinitialize()
{
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
	
	bool bPanelVisible = IVREditorModule::Get().GetVRMode()->GetUISystem().IsShowingEditorUIPanel(CreationContext.PanelID);	

	// Close panel if currently visible
	if (bPanelVisible)
	{
		// Close the existing panel by passing null as the widget. We don't care about any of the other parameters in this case
		CreationContext.WidgetClass = nullptr;		
		CreationContext.PanelSize = FVector2D(1,1); // Guard against 0,0 user input. The actual size is not important when closing a panel, but a check() would trigger
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

bool UVPScoutingSubsystem::IsVRScoutingUIOpen(const FName& PanelID)
{
	return IVREditorModule::Get().GetVRMode()->GetUISystem().IsShowingEditorUIPanel(PanelID);
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


