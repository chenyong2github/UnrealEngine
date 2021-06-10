// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DynamicMeshAABBTree3.h"
#include "FrameTypes.h"
#include "InteractiveTool.h"
//#include "InteractiveToolChange.h"
#include "InteractiveToolBuilder.h"
#include "TargetInterfaces/UVUnwrapDynamicMesh.h"

#include "UVSelectTool.generated.h"

class APreviewGeometryActor;
class ULineSetComponent;
class UMeshSelectionMechanic;
class UToolTargetManager;
class UTransformGizmo;
class UTransformProxy;
class UUVEditorMode;
class UUVToolStateObjectStore;
class UPreviewMesh;
class UUVEditorToolMeshInput;

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
	Edge
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

	virtual void UpdateGizmo();
	virtual void UpdateLivePreviewLines();

	UWorld* TargetWorld;

	UPROPERTY()
	TArray<TObjectPtr<UUVEditorToolMeshInput>> Targets;

	UPROPERTY()
	TObjectPtr<UUVSelectToolProperties> Settings = nullptr;

	UPROPERTY()
	TObjectPtr<UMeshSelectionMechanic> SelectionMechanic = nullptr;

	UPROPERTY()
	UTransformGizmo* TransformGizmo = nullptr;

	TArray<TSharedPtr<FDynamicMeshAABBTree3>> AABBTrees;

	UPROPERTY()
	APreviewGeometryActor* LivePreviewGeometryActor = nullptr;

	UPROPERTY()
	ULineSetComponent* LivePreviewLineSet = nullptr;

	UE::Geometry::FFrame3d InitialGizmoFrame;
	TArray<int32> MovingVids;
	TArray<FVector3d> MovingVertOriginalPositions;
	int32 SelectionTargetIndex;
	TArray<int32> BoundaryEids;

	// We need this flag so that SetGizmoVisibility can be called before Setup() by the tool builder.
	bool bGizmoEnabled = false;
};

///**
// * Wraps a FDynamicMeshChange.
// */
//class UVEDITORTOOLS_API FUVSelectToolChange : public FToolCommandChange
//{
//public:
//	FUVSelectToolChange(TUniquePtr<FDynamicMeshChange> MeshChangeIn)
//		: MeshChange(MoveTemp(MeshChangeIn))
//	{};
//
//	virtual void Apply(UObject* Object) override;
//	virtual void Revert(UObject* Object) override;
//	//virtual bool HasExpired(UObject* Object) const override
//	//{
//	//	return Cast<UGroupEdgeInsertionTool>(Object)->CurrentChangeStamp != ChangeStamp;
//	//}
//	virtual FString ToString() const override
//	{
//		return TEXT("FUVSelectToolChange");
//	}
//
//protected:
//	TUniquePtr<FDynamicMeshChange> MeshChange;
//};
