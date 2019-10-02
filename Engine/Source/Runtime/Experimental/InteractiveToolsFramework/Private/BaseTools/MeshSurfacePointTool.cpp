// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "BaseTools/MeshSurfacePointTool.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"
#include "Components/PrimitiveComponent.h"

#define LOCTEXT_NAMESPACE "UMeshSurfacePointTool"


/*
 * ToolBuilder
 */

bool UMeshSurfacePointToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) == 1;
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
	UActorComponent* ActorComponent = ToolBuilderUtil::FindFirstComponent(SceneState, CanMakeComponentTarget);
	UPrimitiveComponent* MeshComponent = Cast<UPrimitiveComponent>(ActorComponent);
	check(MeshComponent != nullptr);
	NewTool->SetSelection( MakeComponentTarget(MeshComponent) );
}


/*
 * Tool
 */
void UMeshSurfacePointTool::Setup()
{
	UInteractiveTool::Setup();

	bShiftToggle = false;
	bCtrlToggle = false;

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
	return ComponentTarget->HitTest(Ray, OutHit);
}


void UMeshSurfacePointTool::OnBeginDrag(const FRay& Ray)
{

}


void UMeshSurfacePointTool::OnUpdateDrag(const FRay& Ray)
{
	FHitResult OutHit;
	if ( HitTest(Ray, OutHit) ) 
	{
		GetToolManager()->DisplayMessage( 
			FText::Format(LOCTEXT("OnUpdateDragMessage", "UMeshSurfacePointTool::OnUpdateDrag: Hit triangle index {0} at ray distance {1}"),
				FText::AsNumber(OutHit.FaceIndex), FText::AsNumber(OutHit.Distance)),
			EToolMessageLevel::Internal);
	}
}

void UMeshSurfacePointTool::OnEndDrag(const FRay& Ray)
{
	//GetToolManager()->DisplayMessage(TEXT("UMeshSurfacePointTool::OnEndDrag!"), EToolMessageLevel::Internal);
}


void UMeshSurfacePointTool::SetShiftToggle(bool bShiftDown)
{
	bShiftToggle = bShiftDown;
}

void UMeshSurfacePointTool::SetCtrlToggle(bool bCtrlDown)
{
	bCtrlToggle = bCtrlDown;
}


FInputRayHit UMeshSurfacePointTool::BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos)
{
	FHitResult OutHit;
	if (HitTest(PressPos.WorldRay, OutHit))
	{
		return FInputRayHit(OutHit.Distance);
	}
	return FInputRayHit();
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
	Tool->SetCtrlToggle(input.bCtrlKeyDown);
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




#undef LOCTEXT_NAMESPACE