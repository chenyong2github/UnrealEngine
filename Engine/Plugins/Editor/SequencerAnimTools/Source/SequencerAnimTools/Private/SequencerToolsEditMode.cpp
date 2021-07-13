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

bool USequencerToolsEditMode::IsCompatibleWith(FEditorModeID OtherModeID) const
{
	return OtherModeID == FName(TEXT("EM_SequencerMode"), FNAME_Find) || OtherModeID == FName(TEXT("EditMode.ControlRig"), FNAME_Find) || OtherModeID == FName(TEXT("EditMode.ControlRigEditor"), FNAME_Find);
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