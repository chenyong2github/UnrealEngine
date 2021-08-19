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

#include "UVSelectTool.generated.h"

PREDECLARE_GEOMETRY(class FDynamicMeshSelection);
class APreviewGeometryActor;
class FToolCommandChange;
class ULineSetComponent;
class UMeshSelectionMechanic;
class UToolTargetManager;
class UTransformGizmo;
class UTransformProxy;
class UUVEditorMode;
class UUVSelectToolChangeRouter;
class UUVToolStateObjectStore;
class UPreviewMesh;
class UUVEditorToolMeshInput;
class UUVToolEmitChangeAPI;

UCLASS()
class UVEDITORTOOLS_API UUVSelectToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()
public:

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;

	bool bGizmoEnabled = false;

	// This is a pointer so that it can be updated under the builder without
	// having to set it in the mode after initializing targets.
	const TArray<TObjectPtr<UUVEditorToolMeshInput>>* Targets = nullptr;

protected:
	virtual const FToolTargetTypeRequirements& GetTargetRequirements() const override;
};

UENUM()
enum class EUVSelectToolSelectionMode : uint8
{
	Island,
	Edge,
	Vertex,
	Triangle,
	Mesh
};

UCLASS()
class UVEDITORTOOLS_API UUVSelectToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = Options)
	EUVSelectToolSelectionMode SelectionMode = EUVSelectToolSelectionMode::Island;

	//~ TODO: Make this only visible in transform mode
	UPROPERTY(EditAnywhere, Category = Options, AdvancedDisplay)
	bool bUpdatePreviewDuringDrag = true;
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
	virtual void SetGizmoEnabled(bool bEnabledIn);
	
	/**
	 * The tool will operate on the meshes given here.
	 */
	virtual void SetTargets(const TArray<TObjectPtr<UUVEditorToolMeshInput>>& TargetsIn)
	{
		Targets = TargetsIn;
	}

	// Used by undo/redo changes to update the tool state
	void SetSelection(const UE::Geometry::FDynamicMeshSelection& NewSelection, bool bBroadcastOnSelectionChanged);
	void SetGizmoTransform(const FTransform& NewTransform);

	// UInteractiveTool
	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;
	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual void OnTick(float DeltaTime) override;
	virtual bool HasCancel() const override { return false; }
	virtual bool HasAccept() const override { return false; }

protected:
	virtual void OnSelectionChanged();

	// Callbacks we'll receive from the gizmo proxy
	virtual void GizmoTransformChanged(UTransformProxy* Proxy, FTransform Transform);
	virtual void GizmoTransformStarted(UTransformProxy* Proxy);
	virtual void GizmoTransformEnded(UTransformProxy* Proxy);


	virtual void ApplyGizmoTransform();
	virtual void UpdateGizmo();
	virtual void UpdateLivePreviewLines();

	void ConfigureSelectionModeFromControls();

	UWorld* TargetWorld;

	UPROPERTY()
	TArray<TObjectPtr<UUVEditorToolMeshInput>> Targets;

	UPROPERTY()
	TObjectPtr<UUVSelectToolProperties> Settings = nullptr;

	UPROPERTY()
	TObjectPtr<UMeshSelectionMechanic> SelectionMechanic = nullptr;

	UPROPERTY()
	TObjectPtr<UTransformGizmo> TransformGizmo = nullptr;

	TArray<TSharedPtr<FDynamicMeshAABBTree3>> AABBTrees;

	UPROPERTY()
	TObjectPtr<APreviewGeometryActor> LivePreviewGeometryActor = nullptr;

	UPROPERTY()
	TObjectPtr<ULineSetComponent> LivePreviewLineSet = nullptr;

	UPROPERTY()
	TObjectPtr<UUVToolEmitChangeAPI> EmitChangeAPI = nullptr;

	UPROPERTY()
	TObjectPtr<UUVSelectToolChangeRouter> ChangeRouter = nullptr;

	UE::Geometry::FFrame3d InitialGizmoFrame;
	FTransform UnappliedGizmoTransform;
	bool bInDrag = false;
	bool bGizmoTransformNeedsApplication = false;

	TArray<int32> MovingVids;
	TArray<int32> SelectedTids;
	TArray<FVector3d> MovingVertOriginalPositions;
	int32 SelectionTargetIndex;
	TArray<int32> BoundaryEids;

	// We need this flag so that SetGizmoVisibility can be called before Setup() by the tool builder.
	bool bGizmoEnabled = false;
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

/**
 * A helper context object that allows us to inject an undo transaction back in time, which gets
 * used to deal with the fact that our stored selection may become invalidated by an intervening
 * tool, and needs to be cleared in an undoable transaction before that tool runs.
 * 
 * NOTE: It seems likely (especially after "Transform" is no longer a separate tool from "Select")
 * that we may just decide not to try to store a selection on tool shutdown. In that case we will
 * delete this class.
 */
UCLASS()
class UVEDITORTOOLS_API UUVSelectToolSpeculativeChangeAPI : public UUVToolContextObject
{
	GENERATED_BODY()
public:

	/** 
	 * Emits a tool-independent change that does nothing unless a subsequent InsertIntoLastSpeculativeChange
	 * call injects a change.
	 */
	void EmitSpeculativeChange(UObject* TargetObject, UUVToolEmitChangeAPI* EmitChangeAPI, const FText& TransactionName);

	bool HasSpeculativeChange()
	{
		return ContentOfLastSpeculativeChange.IsValid();
	}

	/**
	 * Inserts a change into the place marked by the last EmitSpeculativeChange call.
	 */
	void InsertIntoLastSpeculativeChange(TUniquePtr<FToolCommandChange> ChangeToInsert);

protected:
	TSharedPtr<TUniquePtr<FToolCommandChange>> ContentOfLastSpeculativeChange;
};