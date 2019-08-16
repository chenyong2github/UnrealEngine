// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "BaseTools/MeshSurfacePointTool.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"


/*
 * ToolBuilder
 */

bool UMeshSurfacePointToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return ToolBuilderUtil::CountComponents(SceneState, ToolBuilderUtil::IsMeshDescriptionSourceComponent) == 1;
}

UInteractiveTool* UMeshSurfacePointToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UMeshSurfacePointTool* NewTool = CreateNewTool(SceneState);
	InitializeNewTool(NewTool, SceneState);
	return NewTool;
}

UMeshSurfacePointTool* UMeshSurfacePointToolBuilder::CreateNewTool(const FToolBuilderState& SceneState) const
{
	return NewObject<UMeshSurfacePointTool>(SceneState.ToolManager);
}

void UMeshSurfacePointToolBuilder::InitializeNewTool(UMeshSurfacePointTool* NewTool, const FToolBuilderState& SceneState) const
{
	UActorComponent* MeshComponent = ToolBuilderUtil::FindFirstComponent(SceneState, ToolBuilderUtil::IsMeshDescriptionSourceComponent);
	check(MeshComponent != nullptr);
	NewTool->SetMeshSource(
		SceneState.SourceBuilder->MakeMeshDescriptionSource(MeshComponent) );
}


/*
 * Tool
 */

void UMeshSurfacePointTool::SetMeshSource(TUniquePtr<IMeshDescriptionSource> MeshSourceIn)
{
	this->MeshSource = TUniquePtr<IMeshDescriptionSource>(MoveTemp(MeshSourceIn));
}


void UMeshSurfacePointTool::Setup()
{
	UInteractiveTool::Setup();

	bShiftToggle = false;

	// add input behaviors
	UMeshSurfacePointToolMouseBehavior* mouseBehavior = NewObject<UMeshSurfacePointToolMouseBehavior>();
	mouseBehavior->Initialize(this);
	AddInputBehavior(mouseBehavior);

	UMouseHoverBehavior* hoverBehavior = NewObject<UMouseHoverBehavior>();
	hoverBehavior->Initialize(this);
	AddInputBehavior(hoverBehavior);
}


bool UMeshSurfacePointTool::HitTest(const FRay& Ray, FHitResult& OutHit)
{
	return MeshSource->HitTest(Ray, OutHit);
}


void UMeshSurfacePointTool::OnBeginDrag(const FRay& Ray)
{

}


void UMeshSurfacePointTool::OnUpdateDrag(const FRay& Ray)
{
	FHitResult OutHit;
	if ( HitTest(Ray, OutHit) ) 
	{
		GetToolManager()->PostMessage( 
			FString::Printf(TEXT("[UMeshSurfacePointTool::OnUpdateDrag] Hit triangle index %d at ray distance %f"), OutHit.FaceIndex, OutHit.Distance), EToolMessageLevel::Internal);
	}
}

void UMeshSurfacePointTool::OnEndDrag(const FRay& Ray)
{
	//GetToolManager()->PostMessage(TEXT("UMeshSurfacePointTool::OnEndDrag!"), EToolMessageLevel::Internal);
}


void UMeshSurfacePointTool::SetShiftToggle(bool bShiftDown)
{
	bShiftToggle = bShiftDown;
}




/*
 * Mouse Input Behavior
 */


void UMeshSurfacePointToolMouseBehavior::Initialize(UMeshSurfacePointTool* ToolIn)
{
	this->Tool = ToolIn;
	bInDragCapture = false;
}


FInputCaptureRequest UMeshSurfacePointToolMouseBehavior::WantsCapture(const FInputDeviceState& input)
{
	if ( IsPressed(input) )
	{
		FHitResult OutHit;
		if (Tool->HitTest(input.Mouse.WorldRay, OutHit))
		{
			return FInputCaptureRequest::Begin(this, EInputCaptureSide::Any, OutHit.Distance);
		}
	}
	return FInputCaptureRequest::Ignore();
}

FInputCaptureUpdate UMeshSurfacePointToolMouseBehavior::BeginCapture(const FInputDeviceState& input, EInputCaptureSide eSide)
{
	Tool->SetShiftToggle(input.bShiftKeyDown);
	Tool->OnBeginDrag(input.Mouse.WorldRay);
	LastWorldRay = input.Mouse.WorldRay;
	bInDragCapture = true;
	return FInputCaptureUpdate::Begin(this, EInputCaptureSide::Any);
}

FInputCaptureUpdate UMeshSurfacePointToolMouseBehavior::UpdateCapture(const FInputDeviceState& input, const FInputCaptureData& data)
{
	LastWorldRay = input.Mouse.WorldRay;

	if ( IsReleased(input) ) 
	{
		Tool->OnEndDrag(input.Mouse.WorldRay);
		bInDragCapture = false;
		return FInputCaptureUpdate::End();
	}

	Tool->OnUpdateDrag(input.Mouse.WorldRay);
	return FInputCaptureUpdate::Continue();
}

void UMeshSurfacePointToolMouseBehavior::ForceEndCapture(const FInputCaptureData& data)
{
	if (bInDragCapture)
	{
		Tool->OnEndDrag(LastWorldRay);
		bInDragCapture = false;
	}

	// nothing to do
}






