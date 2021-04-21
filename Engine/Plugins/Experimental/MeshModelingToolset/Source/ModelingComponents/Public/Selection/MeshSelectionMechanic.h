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

	// Use this to initialize the meshes we want to hit test.
	virtual void AddSpatial(TSharedPtr<FDynamicMeshAABBTree3> SpatialIn);

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
	TArray<TSharedPtr<FDynamicMeshAABBTree3>> MeshSpatials;
	FDynamicMeshSelection CurrentSelection;
	FViewCameraState CameraState;
};

