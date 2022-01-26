// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigEditor/IKRigEditMode.h"

#include "EditorViewportClient.h"
#include "AssetEditorModeManager.h"
#include "IPersonaPreviewScene.h"
#include "SkeletalDebugRendering.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RigEditor/IKRigAnimInstance.h"
#include "RigEditor/IKRigHitProxies.h"
#include "RigEditor/IKRigToolkit.h"
#include "IKRigDebugRendering.h"

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
	
	// target union of selected goals and bones
	TArray<FName> OutGoalNames, OutBoneNames;
	Controller->GetSelectedGoalNames(OutGoalNames);
	Controller->GetSelectedBoneNames(OutBoneNames);
	
	if (!OutGoalNames.IsEmpty() || !OutBoneNames.IsEmpty())
	{
		TArray<FVector> GoalPoints;

		// get goal locations
		for (const FName& GoalName : OutGoalNames)
		{			
			GoalPoints.Add(Controller->AssetController->GetGoal(GoalName)->CurrentTransform.GetLocation());
		}

		// get bone locations
		const FIKRigSkeleton& FIKRigSkeleton = Controller->AssetController->GetIKRigSkeleton();
		for (const FName& BoneName : OutBoneNames)
		{
			const int32 BoneIndex = FIKRigSkeleton.GetBoneIndexFromName(BoneName);
			if (BoneIndex != INDEX_NONE)
			{
				TArray<int32> Children;
				FIKRigSkeleton.GetChildIndices(BoneIndex, Children);
				for (int32 ChildIndex: Children)
				{
					GoalPoints.Add(FIKRigSkeleton.CurrentPoseGlobal[ChildIndex].GetLocation());
				}
				GoalPoints.Add(FIKRigSkeleton.CurrentPoseGlobal[BoneIndex].GetLocation());
			}
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
	RenderGoals(PDI);
	RenderBones(PDI);
}

void FIKRigEditMode::RenderGoals(FPrimitiveDrawInterface* PDI)
{
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return; 
	}
	
	const UIKRigController* AssetController = Controller->AssetController;
	const UIKRigDefinition* IKRigAsset = AssetController->GetAsset();
	if (!IKRigAsset->DrawGoals)
	{
		return;
	}
	
	TArray<UIKRigEffectorGoal*> Goals = AssetController->GetAllGoals();
	for (const UIKRigEffectorGoal* Goal : Goals)
	{
		const bool bIsSelected = Controller->IsGoalSelected(Goal->GoalName);
		const float Size = IKRigAsset->GoalSize * Goal->SizeMultiplier;
		const float Thickness = IKRigAsset->GoalThickness * Goal->ThicknessMultiplier;
		PDI->SetHitProxy(new HIKRigEditorGoalProxy(Goal->GoalName));
		IKRigDebugRendering::DrawGoal(PDI, Goal, bIsSelected, Size, Thickness);
		PDI->SetHitProxy(NULL);
	}
}

void FIKRigEditMode::RenderBones(FPrimitiveDrawInterface* PDI)
{
	// editor configured and initialized?
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return; 
	}

	const UIKRigController* AssetController = Controller->AssetController;
	const UIKRigDefinition* IKRigAsset = AssetController->GetAsset();
	if (!IKRigAsset->DrawBones)
	{
		return;
	}

	// anim instance initialized?
	if (!Controller->AnimInstance.IsValid())
	{
		return;
	}

	// IKRig processor initialized and running?
	UIKRigProcessor* CurrentProcessor = Controller->AnimInstance->GetCurrentlyRunningProcessor();
	if (!IsValid(CurrentProcessor))
	{
		return;
	}
	if (!CurrentProcessor->IsInitialized())
	{
		return;
	}

	// get affected / selected bones
	TSet<int32> OutAffectedBones;
	TSet<int32> OutSelectedBones;
	GetAffectedBones(Controller.Get(), CurrentProcessor,OutAffectedBones,OutSelectedBones);

	// draw affected bones
	TArray<int32> ChildrenIndices;
	const FIKRigSkeleton& Skeleton = CurrentProcessor->GetSkeleton();
	const TArray<FTransform>& BoneTransforms = Skeleton.CurrentPoseGlobal;
	const float MaxDrawRadius = Controller->SkelMeshComponent->Bounds.SphereRadius * 0.01f;
	for (int32 BoneIndex=0; BoneIndex<Skeleton.BoneNames.Num(); ++BoneIndex)
	{
		// selected bones are drawn with different color
		const bool bIsSelected = OutSelectedBones.Contains(BoneIndex);
		const bool bIsAffected = OutAffectedBones.Contains(BoneIndex);
		FLinearColor LineColor = bIsAffected ? IKRigDebugRendering::AFFECTED_BONE_COLOR : IKRigDebugRendering::DESELECTED_BONE_COLOR;
		LineColor = bIsSelected ? IKRigDebugRendering::SELECTED_BONE_COLOR : LineColor;

		// only draw axes on affected/selected bones
		const bool bDrawAxes = bIsSelected || bIsAffected;
		
		const float BoneRadiusSetting = IKRigAsset->BoneSize;
		const float BoneRadius = FMath::Min(1.0f, MaxDrawRadius) * BoneRadiusSetting;
		
		// draw line from bone to each child
		FTransform BoneTransform = BoneTransforms[BoneIndex];
		TArray<FVector> ChildPoints;
		Skeleton.GetChildIndices(BoneIndex,ChildrenIndices);
		for (int32 ChildIndex : ChildrenIndices)
		{
			ChildPoints.Add(BoneTransforms[ChildIndex].GetLocation());
		}

		PDI->SetHitProxy(new HIKRigEditorBoneProxy(Skeleton.BoneNames[BoneIndex]));
		IKRigDebugRendering::DrawWireBone(
			PDI,
			BoneTransform,
			ChildPoints,
			LineColor,
			SDPG_Foreground,
			BoneRadius,
			bDrawAxes);
		PDI->SetHitProxy(nullptr);
	}
}

void FIKRigEditMode::GetAffectedBones(
	FIKRigEditorController* Controller,
	UIKRigProcessor* Processor,
	TSet<int32>& OutAffectedBones,
	TSet<int32>& OutSelectedBones) const
{
	OutAffectedBones.Reset();
	OutSelectedBones.Reset();

	const FIKRigSkeleton& Skeleton = Processor->GetSkeleton();
	
	switch (Controller->GetLastSelectedType())
	{
		case EIKRigSelectionType::Hierarchy:
		{
			// get selected bones
			TArray<TSharedPtr<FIKRigTreeElement>> SelectedBoneItems;
			Controller->GetSelectedBones(SelectedBoneItems);

			// record indices of all selected bones
			for (TSharedPtr<FIKRigTreeElement> SelectedBone: SelectedBoneItems)
			{
				int32 BoneIndex = Skeleton.GetBoneIndexFromName(SelectedBone->BoneName);
				OutSelectedBones.Add(BoneIndex);
			}

			// "affected bones" are the selected bones AND their children, recursively
			for (int32 SelectedBone : OutSelectedBones)
			{
				const int32 EndOfBranch = Skeleton.GetCachedEndOfBranchIndex(SelectedBone);
				OutAffectedBones.Add(SelectedBone);
				for (int32 BoneIndex=SelectedBone; BoneIndex<=EndOfBranch; ++BoneIndex)
				{
					OutAffectedBones.Add(BoneIndex);
				}
			}
		}
		break;
		
		case EIKRigSelectionType::SolverStack:
		{
			// get selected solver
			TArray<TSharedPtr<FSolverStackElement>> SelectedSolvers;
			Controller->GetSelectedSolvers(SelectedSolvers);
			if (SelectedSolvers.IsEmpty())
			{
				return;
			}

			// record which bones in the skeleton are affected by this solver
			UIKRigController* AssetController = Controller->AssetController;
			UIKRigSolver* SelectedSolver = AssetController->GetSolver(SelectedSolvers[0].Get()->IndexInStack);
			if(SelectedSolver)
			{
				for (int32 BoneIndex=0; BoneIndex<Skeleton.BoneNames.Num(); ++BoneIndex)
				{
					const FName& BoneName = Skeleton.BoneNames[BoneIndex];
					if (SelectedSolver->IsBoneAffectedBySolver(BoneName, Skeleton))
					{
						OutAffectedBones.Add(BoneIndex);
					}
				}
			}
		}
		break;
		
		case EIKRigSelectionType::RetargetChains:
		{
			const FName SelectedChainName = Controller->GetSelectedChain();
			if (SelectedChainName == NAME_None)
			{
				return;
			}
			Controller->AssetController->ValidateChain(SelectedChainName, OutSelectedBones);
		}
		break;
		
		default:
			checkNoEntry();
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
	
	TArray<FName> OutGoalNames;
	Controller->GetSelectedGoalNames(OutGoalNames);
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
		// draw bones based on the hierarchy when clicking in viewport
		EditorController.Pin()->SetLastSelectedType(EIKRigSelectionType::Hierarchy);
		
		// clicking in empty space clears selection and shows empty details
		if (!HitProxy)
		{
			Controller->ClearSelection();
			return false;
		}
		
		// selected goal
		if (HitProxy->IsA(HIKRigEditorGoalProxy::StaticGetType()))
		{
			HIKRigEditorGoalProxy* GoalProxy = static_cast<HIKRigEditorGoalProxy*>(HitProxy);
			const bool bReplaceSelection = !(InViewportClient->IsCtrlPressed() || InViewportClient->IsShiftPressed());
			Controller->HandleGoalSelectedInViewport(GoalProxy->GoalName, bReplaceSelection);
			return true;
		}
		// selected bone
		if (HitProxy->IsA(HIKRigEditorBoneProxy::StaticGetType()))
		{
			HIKRigEditorBoneProxy* BoneProxy = static_cast<HIKRigEditorBoneProxy*>(HitProxy);
			const bool bReplaceSelection = !(InViewportClient->IsCtrlPressed() || InViewportClient->IsShiftPressed());
			Controller->HandleBoneSelectedInViewport(BoneProxy->BoneName, bReplaceSelection);
			return true;
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
	
	TArray<FName> SelectedGoalNames;
	Controller->GetSelectedGoalNames(SelectedGoalNames);
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

	TArray<FName> SelectedGoalNames;
	Controller->GetSelectedGoalNames(SelectedGoalNames);
	const UIKRigController* AssetController = Controller->AssetController;

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
	
	TArray<FName> SelectedGoalNames;
	Controller->GetSelectedGoalNames(SelectedGoalNames);
	if (SelectedGoalNames.IsEmpty())
	{
		return false; // nothing selected to manipulate
	}

	if (const UIKRigEffectorGoal* Goal = Controller->AssetController->GetGoal(SelectedGoalNames[0]))
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
	
		TArray<FName> SelectedGoalNames;
		Controller->GetSelectedGoalNames(SelectedGoalNames);
		if (SelectedGoalNames.IsEmpty())
		{
			return false; // nothing selected to manipulate
		}

		for (const FName& GoalName : SelectedGoalNames)
		{
			Controller->AssetController->RemoveGoal(GoalName);
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
