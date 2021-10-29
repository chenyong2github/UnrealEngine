// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigEditor/IKRigEditMode.h"

#include "EditorViewportClient.h"
#include "AssetEditorModeManager.h"
#include "IPersonaPreviewScene.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RigEditor/IKRigHitProxies.h"
#include "RigEditor/IKRigToolkit.h"

#define LOCTEXT_NAMESPACE "IKRetargeterEditMode"

FName FIKRigEditMode::ModeName("IKRigAssetEditMode");

FIKRigEditMode::FIKRigEditMode()
{
}

bool FIKRigEditMode::GetCameraTarget(FSphere& OutTarget) const
{
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false; 
	}
	
	// target union of selected goals
	const TArray<FName>& OutGoalNames = Controller->GetSelectedGoals();
	if (!OutGoalNames.IsEmpty())
	{
		TArray<FVector> GoalPoints;
		for (const FName& GoalName : OutGoalNames)
		{			
			GoalPoints.Add(Controller->AssetController->GetGoal(GoalName)->CurrentTransform.GetLocation());
		}

		// create a sphere that contains all the goal points
		OutTarget = FSphere(&GoalPoints[0], GoalPoints.Num());
		return true;
	}

	// target skeletal mesh
	if (Controller->SkelMeshComponent)
	{
		OutTarget = Controller->SkelMeshComponent->Bounds.GetSphere();
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

	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return; 
	}
	
	UIKRigController* AssetController = Controller->AssetController;
	const UIKRigDefinition* IKRigAsset = AssetController->GetAsset();
	TArray<UIKRigEffectorGoal*> Goals = AssetController->GetAllGoals();
	for (const UIKRigEffectorGoal* Goal : Goals)
	{
		const bool bIsSelected = Controller->IsGoalSelected(Goal->GoalName);
		PDI->SetHitProxy(new HIKRigEditorGoalProxy(Goal->GoalName));
		const float Size = IKRigAsset->GoalSize * Goal->SizeMultiplier;
		const float Thickness = IKRigAsset->GoalThickness * Goal->ThicknessMultiplier;
		GoalDrawer.DrawGoal(PDI, Goal, bIsSelected, Size, Thickness);
		PDI->SetHitProxy(NULL);
	}
}

bool FIKRigEditMode::AllowWidgetMove()
{
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false; 
	}
	
	return Controller->GetNumSelectedGoals() > 0;
}

bool FIKRigEditMode::ShouldDrawWidget() const
{
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false; 
	}
	
	return Controller->GetNumSelectedGoals() > 0;
}

bool FIKRigEditMode::UsesTransformWidget() const
{
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false; 
	}
	
	return Controller->GetNumSelectedGoals() > 0;
}

bool FIKRigEditMode::UsesTransformWidget(UE::Widget::EWidgetMode CheckMode) const
{
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false; 
	}
	
	return Controller->GetNumSelectedGoals() > 0;
}

FVector FIKRigEditMode::GetWidgetLocation() const
{
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return FVector::ZeroVector; 
	}
	
	const TArray<FName>& OutGoalNames = Controller->GetSelectedGoals();
	if (OutGoalNames.IsEmpty())
	{
		return FVector::ZeroVector; 
	}

	return Controller->AssetController->GetGoalCurrentTransform(OutGoalNames.Last()).GetTranslation();
}

bool FIKRigEditMode::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false; 
	}
	
	// check for selections
	if (Click.GetKey() == EKeys::LeftMouseButton)
	{
		// selected goal
		if (HitProxy && HitProxy->IsA(HIKRigEditorGoalProxy::StaticGetType()))
		{
			HIKRigEditorGoalProxy* GoalProxy = static_cast<HIKRigEditorGoalProxy*>(HitProxy);
			const bool bReplaceSelection = !(InViewportClient->IsCtrlPressed() || InViewportClient->IsShiftPressed());
			Controller->HandleGoalSelectedInViewport(GoalProxy->GoalName, bReplaceSelection);
			return true;
		}
		else
		{
			// clicking in empty space clears selected goals
			const bool bReplaceSelection = true;
			Controller->HandleGoalSelectedInViewport(NAME_None, bReplaceSelection);
		}
	}
	
	return false;
}

bool FIKRigEditMode::StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false; 
	}
	
	const TArray<FName>& SelectedGoalNames = Controller->GetSelectedGoals();
	if (SelectedGoalNames.IsEmpty())
	{
		return false; // no goals selected to manipulate
	}

	const EAxisList::Type CurrentAxis = InViewportClient->GetCurrentWidgetAxis();
	if (CurrentAxis == EAxisList::None)
	{
		return false; // not manipulating a required axis
	}

	GEditor->BeginTransaction(LOCTEXT("ManipulateGoal", "Manipulate IK Rig Goal"));
	for (const FName& SelectedGoal : SelectedGoalNames)
	{
		Controller->AssetController->ModifyGoal(SelectedGoal);
	}
	Controller->bManipulatingGoals = true;
	return true;
}

bool FIKRigEditMode::EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false; 
	}
	
	if (!Controller->bManipulatingGoals)
	{
		return false; // not handled
	}

	GEditor->EndTransaction();
	Controller->bManipulatingGoals = false;
	return true;
}

bool FIKRigEditMode::InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale)
{
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false; 
	}
	
	if (!Controller->bManipulatingGoals)
	{
		return false; // not handled
	}

	const TArray<FName>& SelectedGoalNames = Controller->GetSelectedGoals();
	UIKRigController* AssetController = Controller->AssetController;

	// translate goals
	if(InViewportClient->GetWidgetMode() == UE::Widget::WM_Translate)
	{
		for (const FName& GoalName : SelectedGoalNames)
		{
			FTransform CurrentTransform = AssetController->GetGoalCurrentTransform(GoalName);
			CurrentTransform.AddToTranslation(InDrag);
			AssetController->SetGoalCurrentTransform(GoalName, CurrentTransform);
		}
	}

	// rotate goals
	if(InViewportClient->GetWidgetMode() == UE::Widget::WM_Rotate)
	{
		for (const FName& GoalName : SelectedGoalNames)
		{
			FTransform CurrentTransform = AssetController->GetGoalCurrentTransform(GoalName);
			FQuat CurrentRotation = CurrentTransform.GetRotation();
			CurrentRotation = (InRot.Quaternion() * CurrentRotation);
			CurrentTransform.SetRotation(CurrentRotation);
			AssetController->SetGoalCurrentTransform(GoalName, CurrentTransform);
		}
	}
	
	return true;
}

bool FIKRigEditMode::GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false;
	}
	
	const TArray<FName>& SelectedGoals = Controller->GetSelectedGoals();
	if (SelectedGoals.IsEmpty())
	{
		return false; // nothing selected to manipulate
	}

	if (const UIKRigEffectorGoal* Goal = Controller->AssetController->GetGoal(SelectedGoals[0]))
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

bool FIKRigEditMode::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	if (FEdMode::InputKey(ViewportClient, Viewport, Key, Event))
	{
		return false;
	}
		
	if (Key == EKeys::Delete || Key == EKeys::BackSpace)
	{
		const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
		if (!Controller.IsValid())
		{
			return false;
		}
	
		TArray<FName> SelectedGoals = Controller->GetSelectedGoals();
		if (SelectedGoals.IsEmpty())
		{
			return false; // nothing selected to manipulate
		}

		for (const FName& GoalName : SelectedGoals)
		{
			Controller->DeleteGoal(GoalName);
		}

		Controller->RefreshAllViews();
		return true;
	}

	return false;
}

void FIKRigEditMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	FEdMode::Tick(ViewportClient, DeltaTime);
}

void FIKRigEditMode::DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
	FEdMode::DrawHUD(ViewportClient, Viewport, View, Canvas);
}

#undef LOCTEXT_NAMESPACE
