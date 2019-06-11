// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "BaseTools/SingleClickTool.h"
#include "InteractiveToolManager.h"


/*
 * ToolBuilder
 */

bool USingleClickToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return true;
}

UInteractiveTool* USingleClickToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	USingleClickTool* NewTool = NewObject<USingleClickTool>(SceneState.ToolManager);
	return NewTool;
}



/*
 * Tool
 */


void USingleClickTool::Setup()
{
	UInteractiveTool::Setup();

	// add default button input behaviors for devices
	USingleClickInputBehavior* MouseBehavior = NewObject<USingleClickInputBehavior>();
	MouseBehavior->Initialize(this);
	AddInputBehavior(MouseBehavior);
}


bool USingleClickTool::IsHitByClick(const FInputDeviceRay& ClickPos)
{
	return true;
}


void USingleClickTool::OnClicked(const FInputDeviceRay& ClickPos)
{
	// print debug message
	GetToolManager()->PostMessage( 
		FString::Printf( TEXT("USingleClickTool::OnClicked at (%f,%f)"), ClickPos.ScreenPosition.X, ClickPos.ScreenPosition.Y), 
		EToolMessageLevel::Internal );
}




