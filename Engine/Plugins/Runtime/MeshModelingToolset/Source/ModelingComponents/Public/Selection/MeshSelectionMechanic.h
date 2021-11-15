// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GeometryBase.h" // Predeclare macros

#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "InteractionMechanic.h"
#include "InteractiveTool.h"
#include "Selection/DynamicMeshSelection.h"
#include "Mechanics/RectangleMarqueeMechanic.h"
#include "ToolContextInterfaces.h" //FViewCameraState

#include "MeshSelectionMechanic.generated.h"

class UTriangleSetComponent;
class ULineSetComponent;
class UPointSetComponent;
class APreviewGeometryActor;
struct FCameraRectangle;

enum class EMeshSelectionMechanicMode
{
	Component,
	
	// Not yet fully implemented for UV mesh purposes, since 
	// we need to be able to select occluded edges
	Edge,
	Vertex,
	Triangle,

	// TODO: This might be good to rename later. And determine how it might interact with multi-mesh selection?
	Mesh
};

UCLASS()
class MODELINGCOMPONENTS_API UMeshSelectionMechanicProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
};

/**
 * Mechanic for selecting elements of a dynamic mesh.
 * 
 * TODO: Currently only able to select unoccluded elements.
 */
UCLASS()
class MODELINGCOMPONENTS_API UMeshSelectionMechanic : public UInteractionMechanic, public IClickBehaviorTarget
{
	GENERATED_BODY()

public:
	using FDynamicMeshAABBTree3 = UE::Geometry::FDynamicMeshAABBTree3;
	using FDynamicMeshSelection = UE::Geometry::FDynamicMeshSelection;

	virtual ~UMeshSelectionMechanic() {}

	virtual void Setup(UInteractiveTool* ParentTool) override;
	virtual void Shutdown() override;

	void SetWorld(UWorld* World);

	// Use this to initialize the meshes we want to hit test.
	virtual void AddSpatial(TSharedPtr<FDynamicMeshAABBTree3> SpatialIn, const FTransform& TransformIn);

	FVector3d GetCurrentSelectionCentroid();

	// Rebuilds the drawn selection highlights, and intializes them in such a way that their transform
	// is equal to StartTransform (useful so that their transform can later be changed)
	void RebuildDrawnElements(const FTransform& StartTransform);

	// Changes the transform of the selection highlights. Useful for quickly updating the hightlight
	// without rebuilding it, when the change is a transformation.
	void SetDrawnElementsTransform(const FTransform& Transform);

	virtual const FDynamicMeshSelection& GetCurrentSelection() const;
	void ChangeSelectionMode(const EMeshSelectionMechanicMode& TargetMode);
	void ChangeSelectionColor(const FColor& TriangleColorIn, float TriangleOpacityIn, const FColor& LineColorIn, const FColor& PointColorIn);
	virtual void SetSelection(const FDynamicMeshSelection& Selection, bool bBroadcast = false, bool bEmitChange = false);

	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual void DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI);

	void OnDragRectangleStarted();
	void OnDragRectangleChanged(const FCameraRectangle& CurrentRectangle);
	void OnDragRectangleFinished(const FCameraRectangle& Rectangle, bool bCancelled);

	// IClickBehaviorTarget implementation
	virtual FInputRayHit IsHitByClick(const FInputDeviceRay& ClickPos) override;
	virtual void OnClicked(const FInputDeviceRay& ClickPos) override;

	// IModifierToggleBehaviorTarget implementation
	virtual void OnUpdateModifierState(int ModifierID, bool bIsOn) override;

	FSimpleMulticastDelegate OnSelectionChanged;

	EMeshSelectionMechanicMode SelectionMode;

	/**
	 * Function to use for emitting selection change events. If not set, Setup() will attach a version that
	 * uses EmitObjectChange() on the tool manager to emit a change that operates on this mechanic.
	 * @param bBroadcastOnSelectionChanged Indicates whether the SetSelection call that asked to issue the
	 *   transaction was also requested to broadcast the OnSelectionChanged delegate. If so, the transaction
	 *   should probably do the same on revert/apply.
	 */ 
	TUniqueFunction<void(const FDynamicMeshSelection& OldSelection, const FDynamicMeshSelection& NewSelection, 
		bool bBroadcastOnSelectionChanged)> EmitSelectionChange;

protected:

	// All four combinations of shift/ctrl down are assigned a behaviour
	bool ShouldAddToSelection() const { return !bCtrlToggle && bShiftToggle; }
	bool ShouldRemoveFromSelection() const { return bCtrlToggle && !bShiftToggle; }
	bool ShouldToggleFromSelection() const { return bCtrlToggle && bShiftToggle; }
	bool ShouldRestartSelection() const { return !bCtrlToggle && !bShiftToggle; }

	UPROPERTY()
	TObjectPtr<URectangleMarqueeMechanic> MarqueeMechanic;

	UPROPERTY()
	TObjectPtr<APreviewGeometryActor> PreviewGeometryActor = nullptr;

	/** The material being displayed for selected triangles */
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> TriangleSetMaterial;

	UPROPERTY()
	TObjectPtr<UTriangleSetComponent> TriangleSet = nullptr;

	UPROPERTY()
	TObjectPtr<ULineSetComponent> LineSet = nullptr;

	UPROPERTY()
	TObjectPtr<UPointSetComponent> PointSet = nullptr;


	TArray<TSharedPtr<FDynamicMeshAABBTree3>> MeshSpatials;
	TArray<FTransform> MeshTransforms;
	FDynamicMeshSelection CurrentSelection;
	FDynamicMeshSelection PreDragSelection;
	int32 CurrentSelectionIndex = IndexConstants::InvalidID;
	FViewCameraState CameraState;

	FColor TriangleColor = FColor::Yellow;
	FColor LineColor = FColor::Yellow;
	FColor PointColor = FColor::Yellow;
	float TriangleOpacity = 0.3;

	bool bShiftToggle = false;
	bool bCtrlToggle = false;
	static const int32 ShiftModifierID = 1;
	static const int32 CtrlModifierID = 2;

	float LineThickness = 3;
	float PointThickness = 6;
	float DepthBias = 0.7;
	float TriangleDepthBias = -0.1;

	FVector3d CurrentSelectionCentroid;
	int32 CentroidTimestamp = -1;
	void UpdateCentroid();

private:
	void ClearCurrentSelection();
	void UpdateCurrentSelection(const TSet<int32>& NewSelection, bool CalledFromOnRectangleChanged = false);
};

