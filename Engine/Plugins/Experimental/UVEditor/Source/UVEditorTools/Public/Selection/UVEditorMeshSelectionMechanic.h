// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GeometryBase.h" // Predeclare macros

#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "InteractionMechanic.h"
#include "InteractiveTool.h"
#include "Selection/UVEditorDynamicMeshSelection.h"
#include "Mechanics/RectangleMarqueeMechanic.h"
#include "ToolContextInterfaces.h" //FViewCameraState

#include "UVEditorMeshSelectionMechanic.generated.h"

class UTriangleSetComponent;
class ULineSetComponent;
class UPointSetComponent;
class APreviewGeometryActor;
class UMaterialInstanceDynamic;
struct FCameraRectangle;

enum class EUVEditorMeshSelectionMode
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
class UVEDITORTOOLS_API UUVEditorMeshSelectionMechanicProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** If true, show the currently hovered selectable primitive */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bShowHoveredElements = true;
};

/**
 * Mechanic for selecting elements of a dynamic mesh in the UV editor.
 * 
 * TODO: Currently only able to select unoccluded elements.
 */
UCLASS()
class UVEDITORTOOLS_API UUVEditorMeshSelectionMechanic : public UInteractionMechanic, public IClickBehaviorTarget
{
	GENERATED_BODY()

public:
	using FDynamicMeshAABBTree3 = UE::Geometry::FDynamicMeshAABBTree3;
	using FUVEditorDynamicMeshSelection = UE::Geometry::FUVEditorDynamicMeshSelection;

	virtual ~UUVEditorMeshSelectionMechanic() {}

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

	virtual const FUVEditorDynamicMeshSelection& GetCurrentSelection() const;
	void ChangeSelectionMode(const EUVEditorMeshSelectionMode& TargetMode);
	virtual void SetSelection(const FUVEditorDynamicMeshSelection& Selection, bool bBroadcast = false, bool bEmitChange = false);

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
	
	TSet<int32> RayCast(const FInputDeviceRay& ClickPos, EUVEditorMeshSelectionMode Mode);

	FSimpleMulticastDelegate OnSelectionChanged;

	EUVEditorMeshSelectionMode SelectionMode;

	/**
	 * Function to use for emitting selection change events. If not set, Setup() will attach a version that
	 * uses EmitObjectChange() on the tool manager to emit a change that operates on this mechanic.
	 * TODO: User should probably be able to specify whether the emitted transaction should broadcast
	 * OnSelectionChanged on redo, undo, or both, to allow selection change events to be used as bookends
	 * around topology changes.
	 */ 
	TUniqueFunction<void(const FUVEditorDynamicMeshSelection& OldSelection, const FUVEditorDynamicMeshSelection& NewSelection)> EmitSelectionChange;

protected:

	// All four combinations of shift/ctrl down are assigned a behaviour
	bool ShouldAddToSelection() const { return !bCtrlToggle && bShiftToggle; }
	bool ShouldRemoveFromSelection() const { return bCtrlToggle && !bShiftToggle; }
	bool ShouldToggleFromSelection() const { return bCtrlToggle && bShiftToggle; }
	bool ShouldRestartSelection() const { return !bCtrlToggle && !bShiftToggle; }

	UPROPERTY()
	TObjectPtr<UUVEditorMeshSelectionMechanicProperties> Settings = nullptr;

	UPROPERTY()
	TObjectPtr<URectangleMarqueeMechanic> MarqueeMechanic;

	UPROPERTY()
	TObjectPtr<APreviewGeometryActor> PreviewGeometryActor = nullptr;

	/** The material being displayed for selected triangles */
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> TriangleSetMaterial = nullptr;
	UPROPERTY()
	TObjectPtr<UTriangleSetComponent> TriangleSet = nullptr;
	UPROPERTY()
	TObjectPtr<ULineSetComponent> LineSet = nullptr;
	UPROPERTY()
	TObjectPtr<UPointSetComponent> PointSet = nullptr;
	
	UPROPERTY()
	TObjectPtr<APreviewGeometryActor> HoverGeometryActor = nullptr;
	
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> HoverTriangleSetMaterial = nullptr;
	UPROPERTY()
	TObjectPtr<UTriangleSetComponent> HoverTriangleSet = nullptr;
	UPROPERTY()
	TObjectPtr<ULineSetComponent> HoverLineSet = nullptr;
	UPROPERTY()
	TObjectPtr<UPointSetComponent> HoverPointSet = nullptr;
	

	TArray<TSharedPtr<FDynamicMeshAABBTree3>> MeshSpatials;
	TArray<FTransform> MeshTransforms;
	FUVEditorDynamicMeshSelection CurrentSelection;
	FUVEditorDynamicMeshSelection PreDragSelection;
	int32 CurrentSelectionIndex = IndexConstants::InvalidID;
	FViewCameraState CameraState;

	bool bShiftToggle = false;
	bool bCtrlToggle = false;
	static const int32 ShiftModifierID = 1;
	static const int32 CtrlModifierID = 2;

	FVector3d CurrentSelectionCentroid;
	int32 CentroidTimestamp = -1;
	void UpdateCentroid();

private:
	void ClearCurrentSelection();
	void UpdateCurrentSelection(const TSet<int32>& NewSelection, bool CalledFromOnRectangleChanged = false);
};

