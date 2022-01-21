// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "FrameTypes.h"
#include "GeometryBase.h"
#include "InteractiveTool.h"
#include "InteractiveToolBuilder.h"
#include "TargetInterfaces/UVUnwrapDynamicMesh.h"
#include "UVToolContextObjects.h"
#include "UVEditorToolAnalyticsUtils.h"
#include "IndexTypes.h"

#include "UVSelectTool.generated.h"

PREDECLARE_GEOMETRY(class FUVEditorDynamicMeshSelection);
class APreviewGeometryActor;
class FToolCommandChange;
class ULineSetComponent;
class UUVEditorMeshSelectionMechanic;
class UPointSetComponent;
class UToolTargetManager;
class UCombinedTransformGizmo;
class UTransformProxy;
class UUVEditorMode;
class UUVSelectToolChangeRouter;
class UUVToolStateObjectStore;
class UPreviewMesh;
class UUVEditorToolMeshInput;
class UUVSeamSewAction;
class UUVIslandConformalUnwrapAction;
class UUVToolEmitChangeAPI;
class UUVToolViewportButtonsAPI;

UCLASS()
class UVEDITORTOOLS_API UUVSelectToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()
public:

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;

	// This is a pointer so that it can be updated under the builder without
	// having to set it in the mode after initializing targets.
	const TArray<TObjectPtr<UUVEditorToolMeshInput>>* Targets = nullptr;
};

UENUM()
enum class ESelectToolAction
{
	NoAction,

	Sew,
	Split,
	IslandConformalUnwrap,
};

UCLASS()
class UVEDITORTOOLS_API USelectToolActionPropertySet : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<UUVSelectTool> ParentTool;

	void Initialize(UUVSelectTool* ParentToolIn) { ParentTool = ParentToolIn; }
	void PostAction(ESelectToolAction Action);

	//~ NOTE: the below comments don't actually get used as tooltips because we use a detail
	//~ customization, and the tooltips are specified in UVEditorCommands.cpp. Change them
	//~ there if needed!

	/** Sew edges. The red edges will be sewn to the green edges */
	UFUNCTION(CallInEditor, Category = Actions)
	void Sew() { PostAction(ESelectToolAction::Sew); }

	/** Given an edge selection, split those edges. Given a vertex selection, split any selected bowtie verts. */
	UFUNCTION(CallInEditor, Category = Actions)
	void Split() { PostAction(ESelectToolAction::Split); };

	/* Apply a conformal unwrap to the selected UV islands */
	UFUNCTION(CallInEditor, Category = Actions)
	void IslandConformalUnwrap();
};



/**
 * A tool for selecting elements of a flat FDynamicMesh corresponding to a UV layer of some asset.
 * If bGizmoEnabled is set to true, the selected elements can be moved around.
 *
 * TODO: Doesn't have undo/redo. Will get broken up into pieces later, probably.
 */
UCLASS()
class UVEDITORTOOLS_API UUVSelectTool : public UInteractiveTool
{
	GENERATED_BODY()

	using FDynamicMeshAABBTree3 = UE::Geometry::FDynamicMeshAABBTree3;

public:
	virtual void SetWorld(UWorld* World) { TargetWorld = World; }
	
	/**
	 * The tool will operate on the meshes given here.
	 */
	virtual void SetTargets(const TArray<TObjectPtr<UUVEditorToolMeshInput>>& TargetsIn)
	{
		Targets = TargetsIn;
	}

	// Used by undo/redo changes to update the tool state
	void SetSelection(const UE::Geometry::FUVEditorDynamicMeshSelection& NewSelection);
	void SetGizmoTransform(const FTransform& NewTransform);

	// UInteractiveTool
	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;
	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual void DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI) override;
	virtual void OnTick(float DeltaTime) override;
	virtual bool HasCancel() const override { return false; }
	virtual bool HasAccept() const override { return false; }

	void RequestAction(ESelectToolAction ActionType);

protected:
	virtual void OnSelectionChanged();
	virtual void ClearWarning();

	// Callbacks we'll receive from the gizmo proxy
	virtual void GizmoTransformChanged(UTransformProxy* Proxy, FTransform Transform);
	virtual void GizmoTransformStarted(UTransformProxy* Proxy);
	virtual void GizmoTransformEnded(UTransformProxy* Proxy);


	virtual void ApplyGizmoTransform();
	virtual void UpdateGizmo();
	virtual void UpdateLivePreviewLines();

	void UpdateSelectionMode();	

	UWorld* TargetWorld;

	UPROPERTY()
	TArray<TObjectPtr<UUVEditorToolMeshInput>> Targets;

	UPROPERTY()
	TObjectPtr<USelectToolActionPropertySet> ToolActions = nullptr;

	UPROPERTY()
	TObjectPtr<UUVToolViewportButtonsAPI> ViewportButtonsAPI = nullptr;

	UPROPERTY()
	TObjectPtr<UUVEditorMeshSelectionMechanic> SelectionMechanic = nullptr;

	UPROPERTY()
	TObjectPtr<UCombinedTransformGizmo> TransformGizmo = nullptr;

	TArray<TSharedPtr<FDynamicMeshAABBTree3>> AABBTrees;

	UPROPERTY()
	TObjectPtr<APreviewGeometryActor> LivePreviewGeometryActor = nullptr;

	UPROPERTY()
	TObjectPtr<ULineSetComponent> LivePreviewLineSet = nullptr;

	UPROPERTY()
	TObjectPtr<UPointSetComponent> LivePreviewPointSet = nullptr;

	UPROPERTY()
	TObjectPtr<UUVToolEmitChangeAPI> EmitChangeAPI = nullptr;

	UPROPERTY()
	TObjectPtr<UUVSelectToolChangeRouter> ChangeRouter = nullptr;

	UPROPERTY()
	TObjectPtr<UUVSeamSewAction> SewAction = nullptr;

	UPROPERTY()
	TObjectPtr<UUVIslandConformalUnwrapAction> IslandConformalUnwrapAction = nullptr;

	UE::Geometry::FFrame3d InitialGizmoFrame;
	FTransform UnappliedGizmoTransform;
	bool bInDrag = false;
	bool bIgnoreOnCanonicalChange = false;
	bool bGizmoTransformNeedsApplication = false;

	TArray<int32> MovingVids;
	TArray<int32> SelectedVids;
	TArray<int32> SelectedTids;
	TArray<FVector3d> MovingVertOriginalPositions;
	int32 SelectionTargetIndex;
	TSet<int32> LivePreviewEids;
	TSet<int32> LivePreviewVids;
	
	// When selecting edges, used to hold the edges as pairs of vids, because the eids change
	// during undo/redo and other topological operations.
	TArray<UE::Geometry::FIndex2i> CurrentSelectionVidPairs;

	ESelectToolAction PendingAction;

	void ApplyAction(ESelectToolAction ActionType);
	void ApplySplit();
	void ApplySplitEdges();
	void ApplySplitBowtieVertices();



	//
	// Analytics
	//

	struct FActionHistoryItem
	{
		FDateTime Timestamp;
		ESelectToolAction ActionType;
		
		// if ActionType == IslandConformalUnwrap then NumOperands is #triangles in island selection
		// if ActionType == Split                 then NumOperands is #edges in selection
		// if ActionType == Sew                   then NumOperands is #edges in selection
		int32 NumOperands = -1;
	};
	
	TArray<FActionHistoryItem> AnalyticsActionHistory;
	UE::Geometry::UVEditorAnalytics::FTargetAnalytics InputTargetAnalytics;
	FDateTime ToolStartTimeAnalytics;
	void RecordAnalytics();
};

/**
 * A helper context object that we can use as the target of undo/redo events to apply them
 * to the current invocation of the select tool (which may have different gizmo/selection
 * pointers than those that were around when the change was emitted).
 */
UCLASS()
class UVEDITORTOOLS_API UUVSelectToolChangeRouter : public UUVToolContextObject
{
	GENERATED_BODY()
public:

	TWeakObjectPtr<UUVSelectTool> CurrentSelectTool = nullptr;
};
