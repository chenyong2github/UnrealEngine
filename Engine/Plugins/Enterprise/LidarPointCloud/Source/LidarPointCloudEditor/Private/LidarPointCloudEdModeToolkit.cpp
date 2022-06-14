// Copyright Epic Games, Inc. All Rights Reserved.

#include "LidarPointCloudEdModeToolkit.h"
#include "EditorModeManager.h"
#include "EdModeInteractiveToolsContext.h"
#include "InteractiveToolManager.h"
#include "LidarPointCloudEditorCommands.h"
#include "LidarPointCloudEdMode.h"
#include "LidarPointCloudShared.h"
#include "Selection.h"
#include "StatusBarSubsystem.h"
#include "Toolkits/AssetEditorModeUILayer.h"

#define LOCTEXT_NAMESPACE "LidarEditMode"

FLidarPointCloudEdModeToolkit::~FLidarPointCloudEdModeToolkit()
{
	GetScriptableEditorMode()->GetInteractiveToolsContext(EToolsContextScope::EdMode)->OnToolNotificationMessage.RemoveAll(this);
	GetScriptableEditorMode()->GetInteractiveToolsContext(EToolsContextScope::EdMode)->OnToolWarningMessage.RemoveAll(this);
}

void FLidarPointCloudEdModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost, TWeakObjectPtr<UEdMode> InOwningMode)
{
	FModeToolkit::Init(InitToolkitHost, InOwningMode);
	
	GetScriptableEditorMode()->GetInteractiveToolsContext(EToolsContextScope::EdMode)->OnToolNotificationMessage.AddSP(this, &FLidarPointCloudEdModeToolkit::SetActiveToolMessage);
	GetScriptableEditorMode()->GetInteractiveToolsContext(EToolsContextScope::EdMode)->OnToolWarningMessage.AddSP(this, &FLidarPointCloudEdModeToolkit::SetActiveToolMessage);
}

FName FLidarPointCloudEdModeToolkit::GetToolkitFName() const
{
	return FName("LidarEditMode");
}

FText FLidarPointCloudEdModeToolkit::GetBaseToolkitName() const
{
	return LOCTEXT( "ToolkitName", "Lidar" );
}

void FLidarPointCloudEdModeToolkit::GetToolPaletteNames(TArray<FName>& InPaletteName) const
{
	InPaletteName.Add(LidarEditorPalletes::Manage);
	InPaletteName.Add(LidarEditorPalletes::Edit);
}

FText FLidarPointCloudEdModeToolkit::GetToolPaletteDisplayName(FName PaletteName) const
{
	if (PaletteName == LidarEditorPalletes::Manage)
	{
		return LOCTEXT("LidarMode_Manage", "Manage");
	}
	if (PaletteName == LidarEditorPalletes::Edit)
	{
		return LOCTEXT("LidarMode_Edit", "Edit");
	}
	return FText();
}

FText FLidarPointCloudEdModeToolkit::GetActiveToolDisplayName() const
{
	if (UInteractiveTool* ActiveTool = GetScriptableEditorMode()->GetToolManager()->GetActiveTool(EToolSide::Left))
	{
		return ActiveTool->GetClass()->GetDisplayNameText();
	}

	return LOCTEXT("LidarNoActiveTool", "LidarNoActiveTool");
}

FText FLidarPointCloudEdModeToolkit::GetActiveToolMessage() const
{
	return ActiveToolMessageCache;
}

void FLidarPointCloudEdModeToolkit::SetActiveToolMessage(const FText& Message)
{
	ActiveToolMessageCache = Message;
	
	if (ModeUILayer.IsValid())
	{
		TSharedPtr<FAssetEditorModeUILayer> ModeUILayerPtr = ModeUILayer.Pin();
		GEditor->GetEditorSubsystem<UStatusBarSubsystem>()->PopStatusBarMessage(ModeUILayerPtr->GetStatusBarName(), ActiveToolMessageHandle);
		ActiveToolMessageHandle = GEditor->GetEditorSubsystem<UStatusBarSubsystem>()->PushStatusBarMessage(ModeUILayerPtr->GetStatusBarName(), Message);
	}

	ActiveToolMessageHandle.Reset();
}


#undef LOCTEXT_NAMESPACE
