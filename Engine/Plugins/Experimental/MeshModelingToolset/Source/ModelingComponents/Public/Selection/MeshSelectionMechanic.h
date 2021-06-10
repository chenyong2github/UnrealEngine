// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GeometryBase.h" // Predeclare macros

#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "DynamicMeshAABBTree3.h"
#include "InteractionMechanic.h"
#include "InteractiveTool.h"
#include "Selection/DynamicMeshSelection.h"
#include "ToolContextInterfaces.h" //FViewCameraState

#include "MeshSelectionMechanic.generated.h"

class ULineSetComponent;
class APreviewGeometryActor;

enum class EMeshSelectionMechanicMode
{
	Component,
	
	// Not yet fully implemented for UV mesh purposes, since 
	// we need to be able to select occluded edges
	Edge
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
 * TODO: Not finished. Needs undo/redo, currently only selects connected components or unoccluded edges.
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
	virtual void SetSelection(const FDynamicMeshSelection& Selection, bool bBroadcast = false, bool bEmitChange = false);

	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	//virtual void DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI); // Will need once we have marquee

	// IClickBehaviorTarget implementation
	virtual FInputRayHit IsHitByClick(const FInputDeviceRay& ClickPos) override;
	virtual void OnClicked(const FInputDeviceRay& ClickPos) override;

	FSimpleMulticastDelegate OnSelectionChanged;

	EMeshSelectionMechanicMode SelectionMode;

protected:

	UPROPERTY()
	APreviewGeometryActor* PreviewGeometryActor = nullptr;

	UPROPERTY()
	ULineSetComponent* LineSet = nullptr;


	TArray<TSharedPtr<FDynamicMeshAABBTree3>> MeshSpatials;
	TArray<FTransform> MeshTransforms;
	FDynamicMeshSelection CurrentSelection;
	int32 CurrentSelectionIndex;
	FViewCameraState CameraState;

	FColor LineColor = FColor::Yellow;
	float LineThickness = 3;
	float DepthBias = 0.3;

	FVector3d CurrentSelectionCentroid;
	void UpdateCentroid();
};

