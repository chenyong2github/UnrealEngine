// Copyright Epic Games, Inc. All Rights Reserved.

#include "LightGizmosModule.h"
#include "Editor.h"
#include "EditorModeRegistry.h"
#include "EditorModeManager.h"
#include "Misc/CoreDelegates.h"
#include "GizmoEdMode.h"
#include "PointLightGizmoFactory.h"
#include "DirectionalLightGizmoFactory.h"
#include "SpotLightGizmoFactory.h"
#include "PointLightGizmo.h"
#include "ScalableConeGizmo.h"
#include "SpotLightGizmo.h"
#include "DirectionalLightGizmo.h"

#define LOCTEXT_NAMESPACE "FLightGizmosModule"

FString FLightGizmosModule::PointLightGizmoType = TEXT("PointLightGizmoType");
FString FLightGizmosModule::SpotLightGizmoType = TEXT("SpotLightGizmoType");
FString FLightGizmosModule::ScalableConeGizmoType = TEXT("ScalableConeGizmoType");
FString FLightGizmosModule::DirectionalLightGizmoType = TEXT("DirectionalLightGizmoType");

void FLightGizmosModule::OnPostEngineInit()
{
	GLevelEditorModeTools().OnEditorModeIDChanged().AddLambda([](const FEditorModeID& ModeID, bool IsEnteringMode) 
	{
		if ((ModeID == GetDefault<UGizmoEdMode>()->GetID()))
		{
			UEdMode* EdMode = GLevelEditorModeTools().GetActiveScriptableMode(GetDefault<UGizmoEdMode>()->GetID());
			UGizmoEdMode* GizmoEdMode = Cast<UGizmoEdMode>(EdMode);

			 // Register the factories and gizmos if we are entering the new Gizmo Mode
			if (IsEnteringMode)
			{
				GizmoEdMode->GetToolManager()->GetPairedGizmoManager()->RegisterGizmoType(PointLightGizmoType, NewObject<UPointLightGizmoBuilder>());
				GizmoEdMode->GetToolManager()->GetPairedGizmoManager()->RegisterGizmoType(ScalableConeGizmoType, NewObject<UScalableConeGizmoBuilder>());
				GizmoEdMode->GetToolManager()->GetPairedGizmoManager()->RegisterGizmoType(SpotLightGizmoType, NewObject<USpotLightGizmoBuilder>());
				GizmoEdMode->GetToolManager()->GetPairedGizmoManager()->RegisterGizmoType(DirectionalLightGizmoType, NewObject<UDirectionalLightGizmoBuilder>());
				GizmoEdMode->AddFactory(NewObject<UPointLightGizmoFactory>());
				GizmoEdMode->AddFactory(NewObject<UDirectionalLightGizmoFactory>());
				GizmoEdMode->AddFactory(NewObject<USpotLightGizmoFactory>());
			}
			else
			{
				//TODO: Deregistering on exit currently causes a crash

				//GizmoEdMode->GetToolManager()->GetPairedGizmoManager()->DeregisterGizmoType(PointLightGizmoType);
				//GizmoEdMode->GetToolManager()->GetPairedGizmoManager()->DeregisterGizmoType(ScalableConeGizmoType);
				//GizmoEdMode->GetToolManager()->GetPairedGizmoManager()->DeregisterGizmoType(SpotLightGizmoType);
				//GizmoEdMode->GetToolManager()->GetPairedGizmoManager()->DeregisterGizmoType(DirectionalLightGizmoType);
			}
			
		}
	});

}

void FLightGizmosModule::StartupModule()
{
	FCoreDelegates::OnPostEngineInit.AddRaw(this, &FLightGizmosModule::OnPostEngineInit);
}

void FLightGizmosModule::ShutdownModule()
{

}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FLightGizmosModule, LightGizmos);
