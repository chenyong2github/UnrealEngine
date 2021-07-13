// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencerAnimToolsModule.h"
#include "EditorModeManager.h"
#include "Editor.h"
#include "EditorModeRegistry.h"
#include "LevelEditor.h"
#include "EdModeInteractiveToolsContext.h"
#include "UnrealEdGlobals.h"
#include "LevelEditorSequencerIntegration.h"

#include "BaseGizmos/TransformGizmoUtil.h"
#include "MotionTrailTool.h"
#include "EditPivotTool.h"
#include "Tools/MotionTrailOptions.h"

#define LOCTEXT_NAMESPACE "FSequencerAnimToolsModule"

namespace UE
{
namespace SequencerAnimTools
{

void FSequencerAnimToolsModule::StartupModule()
{
	FLevelEditorModule& LevelEditor = FModuleManager::Get().LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	LevelEditor.OnLevelEditorCreated().AddRaw(this, &FSequencerAnimToolsModule::OnLevelEditorCreated);
	UMotionTrailToolOptions* MotionTrailOptions = GetMutableDefault<UMotionTrailToolOptions>();
	if (MotionTrailOptions)
	{
		MotionTrailOptions->OnDisplayPropertyChanged.AddRaw(this, &FSequencerAnimToolsModule::OnMotionTralOptionChanged);
	}
}

void FSequencerAnimToolsModule::ShutdownModule()
{
	FLevelEditorModule& LevelEditor = FModuleManager::Get().LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	LevelEditor.OnLevelEditorCreated().RemoveAll(this);
	/* can't do this for some reason the object is already dead (but not nullptr so can't check)
	UMotionTrailToolOptions* MotionTrailOptions = GetMutableDefault<UMotionTrailToolOptions>();
	if (MotionTrailOptions)
	{
		MotionTrailOptions->OnDisplayPropertyChanged.RemoveAll(this);
	}
	*/
}

void FSequencerAnimToolsModule::OnLevelEditorCreated(TSharedPtr<ILevelEditor> InLevelEditor)
{
	LevelEditorPtr = InLevelEditor;
	InLevelEditor->GetEditorModeManager().OnEditorModeIDChanged().AddLambda([this](const FEditorModeID& InModeID, bool IsEnteringMode)
		{
			FEditorModeID ModeID = TEXT("EM_SequencerMode");
			if (ModeID == InModeID && LevelEditorPtr.IsValid())
			{
				//don't re-register, will hit an ensure
				if (AlreadyRegisteredTools.Contains(LevelEditorPtr.Pin().Get()) == false)
				{
					UEdMode* EdMode = LevelEditorPtr.Pin()->GetEditorModeManager().GetActiveScriptableMode(ModeID);

					// Register the factories and gizmos if we are entering the new Gizmo Mode
					if (IsEnteringMode && EdMode)
					{
						LevelEditorPtr.Pin()->GetEditorModeManager().GetInteractiveToolsContext()->ToolManager->RegisterToolType(TEXT("SequencerMotionTrail"), NewObject<UMotionTrailToolBuilder>(EdMode));
						LevelEditorPtr.Pin()->GetEditorModeManager().GetInteractiveToolsContext()->ToolManager->RegisterToolType(TEXT("SequencerPivotTool"), NewObject<USequencerPivotToolBuilder>(EdMode));

						UE::TransformGizmoUtil::RegisterTransformGizmoContextObject(LevelEditorPtr.Pin()->GetEditorModeManager().GetInteractiveToolsContext());

					}
					AlreadyRegisteredTools.Add(LevelEditorPtr.Pin().Get());
				}
			}
		});
}

void FSequencerAnimToolsModule::OnMotionTralOptionChanged(FName PropertyName)
{
	UMotionTrailToolOptions* MotionTrailOptions = GetMutableDefault<UMotionTrailToolOptions>();
	if (MotionTrailOptions)
	{
		if (PropertyName == FName("bShowTrails") && LevelEditorPtr.IsValid())
		{
			static bool bIsChangingTrail = false;

			if (bIsChangingTrail == false)
			{
				bIsChangingTrail = true;
				if (FLevelEditorSequencerIntegration::Get().GetSequencers().Num() > 0)
				{

					if (MotionTrailOptions->bShowTrails)
					{
						LevelEditorPtr.Pin()->GetEditorModeManager().GetInteractiveToolsContext()->ToolManager->SelectActiveToolType(EToolSide::Left, TEXT("SequencerMotionTrail"));
						LevelEditorPtr.Pin()->GetEditorModeManager().GetInteractiveToolsContext()->ToolManager->ActivateTool(EToolSide::Left);
					}
					else
					{
						if (Cast<UMotionTrailTool>(LevelEditorPtr.Pin()->GetEditorModeManager().GetInteractiveToolsContext()->ToolManager->ActiveLeftTool))
						{
							LevelEditorPtr.Pin()->GetEditorModeManager().GetInteractiveToolsContext()->ToolManager->DeactivateTool(EToolSide::Left, EToolShutdownType::Completed);
						}
					}
					
				}
				else
				{
					MotionTrailOptions->bShowTrails = false;
				}
				bIsChangingTrail = false;
			}
		}
	}
}





} // namespace SequencerAnimTools
} // namespace UE

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(UE::SequencerAnimTools::FSequencerAnimToolsModule, SequencerAnimTools)