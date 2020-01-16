// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseTools/MeshSurfacePointTool.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "BaseBehaviors/ClickDragBehavior.h"
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
	NewTool->SetStylusAPI(this->StylusAPI);
	NewTool->SetSelection( MakeComponentTarget(MeshComponent) );
}


static const int UMeshSurfacePointTool_ShiftModifier = 1;
static const int UMeshSurfacePointTool_CtrlModifier = 2;


/*
 * Tool
 */
void UMeshSurfacePointTool::Setup()
{
	UInteractiveTool::Setup();

	bShiftToggle = false;
	bCtrlToggle = false;

	// add input behaviors
	UClickDragInputBehavior* DragBehavior = NewObject<UClickDragInputBehavior>();
	DragBehavior->Initialize(this);
	AddInputBehavior(DragBehavior);

	UMouseHoverBehavior* HoverBehavior = NewObject<UMouseHoverBehavior>();
	HoverBehavior->Modifiers.RegisterModifier(UMeshSurfacePointTool_ShiftModifier, FInputDeviceState::IsShiftKeyDown);
	HoverBehavior->Modifiers.RegisterModifier(UMeshSurfacePointTool_CtrlModifier, FInputDeviceState::IsCtrlKeyDown);
	HoverBehavior->Initialize(this);
	AddInputBehavior(HoverBehavior);
}

void UMeshSurfacePointTool::SetStylusAPI(IToolStylusStateProviderAPI* StylusAPIIn)
{
	this->StylusAPI = StylusAPIIn;
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



void UMeshSurfacePointTool::OnUpdateModifierState(int ModifierID, bool bIsOn)
{
	if (ModifierID == UMeshSurfacePointTool_ShiftModifier)
	{
		bShiftToggle = bIsOn;
	}
	else if (ModifierID == UMeshSurfacePointTool_CtrlModifier)
	{
		bCtrlToggle = bIsOn;
	}
}


FInputRayHit UMeshSurfacePointTool::CanBeginClickDragSequence(const FInputDeviceRay& PressPos)
{
	FHitResult OutHit;
	if (HitTest(PressPos.WorldRay, OutHit))
	{
		return FInputRayHit(OutHit.Distance);
	}
	return FInputRayHit();
}

void UMeshSurfacePointTool::OnClickPress(const FInputDeviceRay& PressPos)
{
	LastWorldRay = PressPos.WorldRay;
	OnBeginDrag(PressPos.WorldRay);
}

void UMeshSurfacePointTool::OnClickDrag(const FInputDeviceRay& DragPos)
{
	LastWorldRay = DragPos.WorldRay;
	OnUpdateDrag(DragPos.WorldRay);
}

void UMeshSurfacePointTool::OnClickRelease(const FInputDeviceRay& ReleasePos)
{
	LastWorldRay = ReleasePos.WorldRay;
	OnEndDrag(ReleasePos.WorldRay);
}

void UMeshSurfacePointTool::OnTerminateDragSequence()
{
	OnEndDrag(LastWorldRay);
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


float UMeshSurfacePointTool::GetCurrentDevicePressure() const
{
	return (StylusAPI != nullptr) ? FMath::Clamp(StylusAPI->GetCurrentPressure(), 0.0f, 1.0f) : 1.0f;
}



#undef LOCTEXT_NAMESPACE