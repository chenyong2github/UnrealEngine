// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Retargeter/IKRetargeter.h"
#include "IPersonaEditMode.h"

class UIKRigProcessor;
class FIKRetargetEditorController;
class FIKRetargetEditor;
class FIKRetargetPreviewScene;

enum FIKRetargetTrackingState : int8
{
	None,
	RotatingBone,
	TranslatingRoot,
};

struct BoneEdit
{
	FName Name;							// name of last selected bone
	int32 Index;						// index of last selected bone
	FTransform ParentGlobalTransform;	// global transform of parent of last selected bone
	FTransform GlobalTransform;			// global transform of last selected bone
	FTransform LocalTransform;			// local transform of last selected bone
	FQuat AccumulatedGlobalOffset;		// the accumulated offset from rotation gizmo
	
	TArray<FQuat> PrevLocalOffsets;		// the prev stored local offsets of all selected bones
	TArray<FName> SelectedBones;		// the currently selected bones in the viewport
};

class FIKRetargetEditMode : public IPersonaEditMode
{
public:
	static FName ModeName;
	
	FIKRetargetEditMode() = default;

	/** glue for all the editor parts to communicate */
	void SetEditorController(const TSharedPtr<FIKRetargetEditorController> InEditorController) { EditorController = InEditorController; };

	/** IPersonaEditMode interface */
	virtual bool GetCameraTarget(FSphere& OutTarget) const override;
	virtual class IPersonaPreviewScene& GetAnimPreviewScene() const override;
	virtual void GetOnScreenDebugInfo(TArray<FText>& OutDebugInfo) const override;
	/** END IPersonaEditMode interface */

	/** FEdMode interface */
	virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) override;
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;
	virtual void DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas) override;
	virtual bool IsCompatibleWith(FEditorModeID OtherModeID) const override { return true; }
	virtual bool AllowWidgetMove() override;
	virtual bool ShouldDrawWidget() const override;
	virtual bool UsesTransformWidget() const override;
	virtual bool UsesTransformWidget(UE::Widget::EWidgetMode CheckMode) const override;
	virtual FVector GetWidgetLocation() const override;
	virtual bool HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click) override;
	virtual bool StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override;
	virtual bool EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override;
	virtual bool InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale) override;
	virtual bool GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData) override;
	virtual bool GetCustomInputCoordinateSystem(FMatrix& InMatrix, void* InData) override;
	/** END FEdMode interface */

private:

	void GetAffectedBones(
		FIKRetargetEditorController* Controller,
		UIKRigProcessor* Processor,
		TSet<int32>& OutAffectedBones,
		TSet<int32>& OutSelectedBones) const;

	UE::Widget::EWidgetMode CurrentWidgetMode;

	bool IsRootSelected() const;
	bool IsOnlyRootSelected() const;
	bool IsBoneSelected(const FName& BoneName) const;

	BoneEdit BoneEdit;
	void UpdateWidgetTransform();
	void HandleBoneSelectedInViewport(const FName& BoneName, bool bReplace);
	
	/** The hosting app */
	TWeakPtr<FIKRetargetEditorController> EditorController;

	/** viewport selection/editing state */
	FIKRetargetTrackingState TrackingState;
};
