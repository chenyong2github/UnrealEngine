// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "BaseTools/ClickDragTool.h"
#include "InteractiveToolManager.h"
#include "BaseBehaviors/ClickDragBehavior.h"


/*
 * ToolBuilder
 */

bool UClickDragToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return true;
}

UInteractiveTool* UClickDragToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UClickDragTool* NewTool = NewObject<UClickDragTool>(SceneState.ToolManager);
	return NewTool;
}



/*
 * Tool
 */


void UClickDragTool::Setup()
{
	UInteractiveTool::Setup();

	// add default mouse input behavior
	UClickDragInputBehavior* MouseBehavior = NewObject<UClickDragInputBehavior>();
	MouseBehavior->Initialize(this);
	AddInputBehavior(MouseBehavior);
}


bool UClickDragTool::CanBeginClickDragSequence(const FInputDeviceRay& ClickPos)
{
	return true;
}


void UClickDragTool::OnClickPress(const FInputDeviceRay& ClickPos)
{
	// print debug message
	GetToolManager()->PostMessage( 
		FString::Printf( TEXT("UClickDragTool::OnClickPress at (%f,%f)"), ClickPos.ScreenPosition.X, ClickPos.ScreenPosition.Y), 
		EToolMessageLevel::Internal );
}

void UClickDragTool::OnClickDrag(const FInputDeviceRay& ClickPos)
{
}


void UClickDragTool::OnClickRelease(const FInputDeviceRay& ClickPos)
{
	// print debug message
	GetToolManager()->PostMessage(
		FString::Printf(TEXT("UClickDragTool::OnClickRelease at (%f,%f)"), ClickPos.ScreenPosition.X, ClickPos.ScreenPosition.Y),
		EToolMessageLevel::Internal);
}


void UClickDragTool::OnTerminateDragSequence()
{

}