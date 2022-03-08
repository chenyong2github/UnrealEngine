// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GeometryBase.h" // Predeclare macros

#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "InteractionMechanic.h"
#include "InteractiveTool.h"
#include "Selection/UVToolSelection.h"
#include "Selection/UVToolSelectionAPI.h" // EUVEditorSelectionMode
#include "Mechanics/RectangleMarqueeMechanic.h"
#include "ToolContextInterfaces.h" //FViewCameraState

#include "UVEditorMeshSelectionMechanic.generated.h"

class APreviewGeometryActor;
struct FCameraRectangle;
class ULineSetComponent;
class UMaterialInstanceDynamic;
class UPointSetComponent;
class UTriangleSetComponent;
class UUVToolViewportButtonsAPI;


/**
 * Mechanic for selecting elements of a dynamic mesh in the UV editor. Interacts
 * heavily with UUVToolSelectionAPI, which actually stores selections.
 */
UCLASS()
class UVEDITORTOOLS_API UUVEditorMeshSelectionMechanic : public UInteractionMechanic, 
	public IClickBehaviorTarget,
	public IHoverBehaviorTarget
{
	GENERATED_BODY()

public:
	using FDynamicMeshAABBTree3 = UE::Geometry::FDynamicMeshAABBTree3;
	using FUVToolSelection = UE::Geometry::FUVToolSelection;

	virtual ~UUVEditorMeshSelectionMechanic() {}

	virtual void Setup(UInteractiveTool* ParentTool) override;
	virtual void Shutdown() override;

	// Initialization functions.
	// The selection API is provided as a parameter rather than being grabbed out of the context
	// store mainly because UVToolSelectionAPI itself sets up a selection mechanic, and is not 
	// yet in the context store when it does this. 
	void Initialize(UWorld* World, UUVToolSelectionAPI* SelectionAPI);
	void SetTargets(const TArray<TObjectPtr<UUVEditorToolMeshInput>>& TargetsIn);

	void SetIsEnabled(bool bIsEnabled);
	bool IsEnabled() { return bIsEnabled; };

	void SetShowHoveredElements(bool bShow);

	using ESelectionMode = UUVToolSelectionAPI::EUVEditorSelectionMode;
	using FModeChangeOptions = UUVToolSelectionAPI::FSelectionMechanicModeChangeOptions;
	/**
	 * Sets selection mode for the mechanic.
	 */
	void SetSelectionMode(ESelectionMode TargetMode,
		const FModeChangeOptions& Options = FModeChangeOptions());

	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual void DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI);

	// IClickBehaviorTarget implementation
	virtual FInputRayHit IsHitByClick(const FInputDeviceRay& ClickPos) override;
	virtual void OnClicked(const FInputDeviceRay& ClickPos) override;

	// IModifierToggleBehaviorTarget implementation
	virtual void OnUpdateModifierState(int ModifierID, bool bIsOn) override;

	// IHoverBehaviorTarget implementation
	virtual FInputRayHit BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos) override;
	virtual void OnBeginHover(const FInputDeviceRay& DevicePos) override;
	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;
	virtual void OnEndHover() override;

	/**
	 * Broadcasted whenever the marquee mechanic rectangle is changed, since these changes
	 * don't trigger normal selection broadcasts.
	 */ 
	FSimpleMulticastDelegate OnDragSelectionChanged;

protected:

	UPROPERTY()
	TObjectPtr<UUVToolSelectionAPI> SelectionAPI = nullptr;

	UPROPERTY()
	TObjectPtr<UUVToolViewportButtonsAPI> ViewportButtonsAPI = nullptr;

	UPROPERTY()
	TObjectPtr<UUVToolEmitChangeAPI> EmitChangeAPI = nullptr;

	UPROPERTY()
	TObjectPtr<URectangleMarqueeMechanic> MarqueeMechanic = nullptr;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> HoverTriangleSetMaterial = nullptr;

	UPROPERTY()
	TObjectPtr<APreviewGeometryActor> HoverGeometryActor = nullptr;
	// Weak pointers so that they go away when geometry actor is destroyed
	TWeakObjectPtr<UTriangleSetComponent> HoverTriangleSet = nullptr;
	TWeakObjectPtr<ULineSetComponent> HoverLineSet = nullptr;
	TWeakObjectPtr<UPointSetComponent> HoverPointSet = nullptr;

	// Should be the same as the mode-level targets array, indexed by AssetID
	TArray<TObjectPtr<UUVEditorToolMeshInput>> Targets;
	TArray<TSharedPtr<FDynamicMeshAABBTree3>> MeshSpatials; // 1:1 with Targets

	ESelectionMode SelectionMode;
	bool bIsEnabled = false;
	bool bShowHoveredElements = true;

	bool GetHitTid(const FInputDeviceRay& ClickPos, int32& TidOut,
		int32& AssetIDOut, int32* ExistingSelectionObjectIndexOut = nullptr);
	void ModifyExistingSelection(TSet<int32>& SelectionSetToModify, const TArray<int32>& SelectedIDs);

	FViewCameraState CameraState;

	bool bShiftToggle = false;
	bool bCtrlToggle = false;
	static const int32 ShiftModifierID = 1;
	static const int32 CtrlModifierID = 2;

	// All four combinations of shift/ctrl down are assigned a behaviour
	bool ShouldAddToSelection() const { return !bCtrlToggle && bShiftToggle; }
	bool ShouldRemoveFromSelection() const { return bCtrlToggle && !bShiftToggle; }
	bool ShouldToggleFromSelection() const { return bCtrlToggle && bShiftToggle; }
	bool ShouldRestartSelection() const { return !bCtrlToggle && !bShiftToggle; }

	// For marquee mechanic
	void OnDragRectangleStarted();
	void OnDragRectangleChanged(const FCameraRectangle& CurrentRectangle);
	void OnDragRectangleFinished(const FCameraRectangle& Rectangle, bool bCancelled);
	TArray<FUVToolSelection> PreDragSelections;
	// Maps asset id to a pre drag selection so that it is easy to tell which assets
	// started with a selection. 1:1 with Targets.
	TArray<const FUVToolSelection*> AssetIDToPreDragSelection;
};

