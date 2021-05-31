// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigEditor/IKRigEditMode.h"

#include "EditorViewportClient.h"
#include "AssetEditorModeManager.h"
#include "IPersonaPreviewScene.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RigEditor/IKRigHitProxies.h"
#include "RigEditor/IKRigToolkit.h"

FName FIKRigEditMode::ModeName("IKRigAssetEditMode");

FIKRigEditMode::FIKRigEditMode()
{
}

bool FIKRigEditMode::GetCameraTarget(FSphere& OutTarget) const
{
	// target union of selected goals
	const TArray<FName>& OutGoalNames = EditorController.Pin()->GetSelectedGoals();
	if (!OutGoalNames.IsEmpty())
	{
		TArray<FVector> GoalPoints;
		for (const auto& GoalName : OutGoalNames)
		{
			FTransform GoalTransform = EditorController.Pin()->AssetController->GetGoalInitialTransform(GoalName);
			GoalPoints.Add(GoalTransform.GetLocation());
		}

		// create a sphere that contains all the goal points
		OutTarget = FSphere(&GoalPoints[0], GoalPoints.Num());
		return true;
	}

	// target skeletal mesh
	if (EditorController.Pin()->SkelMeshComponent)
	{
		OutTarget = EditorController.Pin()->SkelMeshComponent->Bounds.GetSphere();
		return true;
	}
	
	return false;
}

IPersonaPreviewScene& FIKRigEditMode::GetAnimPreviewScene() const
{
	return *static_cast<IPersonaPreviewScene*>(static_cast<FAssetEditorModeManager*>(Owner)->GetPreviewScene());
}

void FIKRigEditMode::GetOnScreenDebugInfo(TArray<FText>& OutDebugInfo) const
{
	// todo: provide warnings from solvers
}

void FIKRigEditMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	FEdMode::Render(View, Viewport, PDI);

	UIKRigController* Controller = EditorController.Pin()->AssetController;
	TArray<UIKRigEffectorGoal*> Goals = Controller->GetAllGoals();
	for (const UIKRigEffectorGoal* Goal : Goals)
	{
		const bool bIsSelected = EditorController.Pin()->IsGoalSelected(Goal->GoalName);
		PDI->SetHitProxy(new HIKRigEditorGoalProxy(Goal->GoalName));
		GoalDrawer.DrawGoal(PDI, Goal, bIsSelected);
		PDI->SetHitProxy(NULL);
	}
}

bool FIKRigEditMode::AllowWidgetMove()
{
	return EditorController.Pin()->GetNumSelectedGoals() > 0;
}

bool FIKRigEditMode::ShouldDrawWidget() const
{
	return EditorController.Pin()->GetNumSelectedGoals() > 0;
}

bool FIKRigEditMode::UsesTransformWidget() const
{
	return EditorController.Pin()->GetNumSelectedGoals() > 0;
}

bool FIKRigEditMode::UsesTransformWidget(UE::Widget::EWidgetMode CheckMode) const
{
	return EditorController.Pin()->GetNumSelectedGoals() > 0;
}

FVector FIKRigEditMode::GetWidgetLocation() const
{
	const TArray<FName>& OutGoalNames = EditorController.Pin()->GetSelectedGoals();
	if (OutGoalNames.IsEmpty())
	{
		return FVector::ZeroVector; 
	}

	return EditorController.Pin()->AssetController->GetGoalCurrentTransform(OutGoalNames.Last()).GetTranslation();
}

bool FIKRigEditMode::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	// check for selections
	if (Click.GetKey() == EKeys::LeftMouseButton)
	{
		// selected goal
		if (HitProxy && HitProxy->IsA(HIKRigEditorGoalProxy::StaticGetType()))
		{
			HIKRigEditorGoalProxy* GoalProxy = static_cast<HIKRigEditorGoalProxy*>(HitProxy);
			const bool bReplaceSelection = !(InViewportClient->IsCtrlPressed() || InViewportClient->IsShiftPressed());
			EditorController.Pin()->HandleGoalSelectedInViewport(GoalProxy->GoalName, bReplaceSelection);
			return true;
		}else
		{
			// clicking in empty space clears selected goals
			const bool bReplaceSelection = true;
			EditorController.Pin()->HandleGoalSelectedInViewport(NAME_None, bReplaceSelection);
		}
	}
	
	return false;
}

bool FIKRigEditMode::StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	const TArray<FName>& SelectedGoalNames = EditorController.Pin()->GetSelectedGoals();
	if (SelectedGoalNames.IsEmpty())
	{
		return false; // no goals selected to manipulate
	}

	const EAxisList::Type CurrentAxis = InViewportClient->GetCurrentWidgetAxis();
	if (CurrentAxis == EAxisList::None)
	{
		return false; // not manipulating a required axis
	}
	
	EditorController.Pin()->bManipulatingGoals = true;
	return true;
}

bool FIKRigEditMode::EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	if (!EditorController.Pin()->bManipulatingGoals)
	{
		return false; // not handled
	}

	EditorController.Pin()->bManipulatingGoals = false;
	return true;
}

bool FIKRigEditMode::InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale)
{
	if (!EditorController.Pin()->bManipulatingGoals)
	{
		return false; // not handled
	}

	// translate goals
	if(InViewportClient->GetWidgetMode() == UE::Widget::WM_Translate)
	{
		const TArray<FName>& SelectedGoalNames = EditorController.Pin()->GetSelectedGoals();
		UIKRigController* Controller = EditorController.Pin()->AssetController;
		for (const FName& GoalName : SelectedGoalNames)
		{
			FTransform CurrentTransform = Controller->GetGoal(GoalName)->CurrentTransform;
			CurrentTransform.AddToTranslation(InDrag);
			Controller->SetGoalCurrentTransform(GoalName, CurrentTransform);
		}
	}

	// rotate goals
	if(InViewportClient->GetWidgetMode() == UE::Widget::WM_Rotate)
	{
		const TArray<FName>& SelectedGoalNames = EditorController.Pin()->GetSelectedGoals();
		UIKRigController* Controller = EditorController.Pin()->AssetController;
		for (const FName& GoalName : SelectedGoalNames)
		{
			FTransform CurrentTransform = Controller->GetGoal(GoalName)->CurrentTransform;
			FQuat CurrentRotation = CurrentTransform.GetRotation();
			CurrentRotation = (InRot.Quaternion() * CurrentRotation);
			CurrentTransform.SetRotation(CurrentRotation);
			Controller->SetGoalCurrentTransform(GoalName, CurrentTransform);
		}
	}
	
	return true;
}

bool FIKRigEditMode::GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	const TArray<FName>& SelectedGoals = EditorController.Pin()->GetSelectedGoals();
	if (SelectedGoals.IsEmpty())
	{
		return false; // nothing selected to manipulate
	}

	if (UIKRigEffectorGoal* Goal = EditorController.Pin()->AssetController->GetGoal(SelectedGoals[0]))
	{
		InMatrix = Goal->CurrentTransform.ToMatrixNoScale().RemoveTranslation();
		return true;
	}

	return false;
}

bool FIKRigEditMode::GetCustomInputCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	return GetCustomDrawingCoordinateSystem(InMatrix, InData);
}

void FIKRigEditMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	FEdMode::Tick(ViewportClient, DeltaTime);
}

void FIKRigEditMode::DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
	FEdMode::DrawHUD(ViewportClient, Viewport, View, Canvas);
}

