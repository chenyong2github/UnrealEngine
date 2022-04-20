// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencerToolsEditMode.h"
#include "EditorModeManager.h"
#include "EdModeInteractiveToolsContext.h"
#include "EdMode.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "EditorModeManager.h"
#include "InteractiveToolManager.h"
#include "BaseSequencerAnimTool.h"
#include "BaseGizmos/TransformGizmoUtil.h"
#include "MotionTrailTool.h"
#include "SequencerAnimEditPivotTool.h"

#define LOCTEXT_NAMESPACE "SequencerAnimTools"


FEditorModeID USequencerToolsEditMode::ModeName = TEXT("SequencerToolsEditMode");


USequencerToolsEditMode::USequencerToolsEditMode() 
{
	Info = FEditorModeInfo(
		ModeName,
		LOCTEXT("ModeName", "Sequencer Tools"),
		FSlateIcon(),
		false
	);
}

USequencerToolsEditMode::~USequencerToolsEditMode()
{

}

void USequencerToolsEditMode::Enter()
{
	FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor"));
	
	if (LevelEditorModule)
	{
		TWeakPtr<ILevelEditor> LevelEditorPtr = LevelEditorModule->GetLevelEditorInstance();
		
		if (LevelEditorPtr.IsValid())
		{
			LevelEditorPtr.Pin()->GetEditorModeManager().GetInteractiveToolsContext()->ToolManager->RegisterToolType(TEXT("SequencerMotionTrail"), NewObject<UMotionTrailToolBuilder>(this));
			LevelEditorPtr.Pin()->GetEditorModeManager().GetInteractiveToolsContext()->ToolManager->RegisterToolType(TEXT("SequencerPivotTool"), NewObject<USequencerPivotToolBuilder>(this));

			UE::TransformGizmoUtil::RegisterTransformGizmoContextObject(LevelEditorPtr.Pin()->GetEditorModeManager().GetInteractiveToolsContext());
		}
	}
}

void USequencerToolsEditMode::Exit()
{
	FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor"));
	
	if (LevelEditorModule)
	{
		TWeakPtr<ILevelEditor> LevelEditorPtr = LevelEditorModule->GetLevelEditorInstance();
		
		if (LevelEditorPtr.IsValid())
		{
			LevelEditorPtr.Pin()->GetEditorModeManager().GetInteractiveToolsContext()->ToolManager->UnregisterToolType(TEXT("SequencerMotionTrail"));
			LevelEditorPtr.Pin()->GetEditorModeManager().GetInteractiveToolsContext()->ToolManager->UnregisterToolType(TEXT("SequencerPivotTool"));

			// TODO: cannot deregister here due to a bug where another mode's Enter() is called before our Exit() on mode switch, and we end
			// up deregistering a helper that the other mode was relying on. Could uncomment the below when that bug is fixed.
			//UE::TransformGizmoUtil::DeregisterTransformGizmoContextObject(LevelEditorPtr.Pin()->GetEditorModeManager().GetInteractiveToolsContext());
		}
	}
}

bool USequencerToolsEditMode::IsCompatibleWith(FEditorModeID OtherModeID) const
{
	// Compatible with all modes similar to FSequencerEdMode
	return true;
}

//If we have one of our own active tools we pass input to it's commands.
bool USequencerToolsEditMode::InputKey(FEditorViewportClient* InViewportClient, FViewport* InViewport, FKey InKey, EInputEvent InEvent) 
{
	if (InEvent != IE_Released)
	{
		//	MZ doesn't seem needed, need more testing  TGuardValue<FEditorViewportClient*> ViewportGuard(CurrentViewportClient, InViewportClient);
		if (FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor")))
		{
			TSharedPtr<ILevelEditor> LevelEditor = LevelEditorModule->GetLevelEditorInstance().Pin();

			if (LevelEditor.IsValid())
			{
				if (IBaseSequencerAnimTool* Tool = Cast<IBaseSequencerAnimTool>(LevelEditor->GetEditorModeManager().GetInteractiveToolsContext()->ToolManager->GetActiveTool(EToolSide::Left)))
				{
					if (Tool->ProcessCommandBindings(InKey, (InEvent == IE_Repeat)))
					{
						return true;
					}
				}
			}
		}
	}	
	return UBaseLegacyWidgetEdMode::InputKey(InViewportClient, InViewport, InKey, InEvent);
}

#undef LOCTEXT_NAMESPACE