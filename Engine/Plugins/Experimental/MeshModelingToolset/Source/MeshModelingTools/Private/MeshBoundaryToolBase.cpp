// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshBoundaryToolBase.h"

#include "BaseBehaviors/SingleClickBehavior.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "DynamicMesh3.h"
#include "InteractiveToolManager.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "Selection/PolygonSelectionMechanic.h"

#define LOCTEXT_NAMESPACE "UMeshBoundaryToolBase"

void UMeshBoundaryToolBase::Setup()
{
	USingleSelectionTool::Setup();

	if (!ComponentTarget)
	{
		return;
	}

	// create mesh to operate on
	OriginalMesh = MakeShared<FDynamicMesh3>();
	FMeshDescriptionToDynamicMesh Converter;
	Converter.Convert(ComponentTarget->GetMesh(), *OriginalMesh);

	LoopSelectClickBehavior = NewObject<USingleClickInputBehavior>();
	LoopSelectClickBehavior->Initialize(this);
	AddInputBehavior(LoopSelectClickBehavior);

	UMouseHoverBehavior* HoverBehavior = NewObject<UMouseHoverBehavior>();
	HoverBehavior->Initialize(this);
	AddInputBehavior(HoverBehavior);

	// initialize hit query
	MeshSpatial.SetMesh(OriginalMesh.Get());

	// initialize topology
	Topology = MakeUnique<FBasicTopology>(OriginalMesh.Get(), false);
	bool bTopologyOK = Topology->RebuildTopology();

	// Set up selection mechanic to find and select edges
	SelectionMechanic = NewObject<UPolygonSelectionMechanic>(this);
	SelectionMechanic->bAddSelectionFilterPropertiesToParentTool = false;
	SelectionMechanic->Setup(this);
	SelectionMechanic->Properties->bSelectEdges = true;
	SelectionMechanic->Properties->bSelectFaces = false;
	SelectionMechanic->Properties->bSelectVertices = false;
	SelectionMechanic->Initialize(OriginalMesh.Get(),
		ComponentTarget->GetWorldTransform(),
		TargetWorld,
		Topology.Get(),
		[this]() { return &MeshSpatial; },
		[this]() { return ShouldSelectionAppend(); }
	);
}

void UMeshBoundaryToolBase::Shutdown(EToolShutdownType ShutdownType)
{
	if (SelectionMechanic)
	{
		SelectionMechanic->Shutdown();
	}
}

void UMeshBoundaryToolBase::Render(IToolsContextRenderAPI* RenderAPI)
{
	if (SelectionMechanic)
	{
		SelectionMechanic->Render(RenderAPI);
	}
}


FInputRayHit UMeshBoundaryToolBase::IsHitByClick(const FInputDeviceRay& ClickPos)
{
	FHitResult OutHit;
	if (SelectionMechanic->TopologyHitTest(ClickPos.WorldRay, OutHit))
	{
		return FInputRayHit(OutHit.Distance);
	}
	FInputRayHit Result(TNumericLimits<float>::Max());
	Result.bHit = false;
	return Result;
}

void UMeshBoundaryToolBase::OnClicked(const FInputDeviceRay& ClickPos)
{
	// update selection
	GetToolManager()->BeginUndoTransaction(LOCTEXT("BoundarySelectionChange", "Selection"));
	SelectionMechanic->BeginChange();

	FVector3d LocalHitPosition, LocalHitNormal;
	bool bSelectionModified = SelectionMechanic->UpdateSelection(ClickPos.WorldRay, LocalHitPosition, LocalHitNormal);

	if (bSelectionModified)
	{
		OnSelectionChanged();
	}

	SelectionMechanic->EndChangeAndEmitIfModified();
	GetToolManager()->EndUndoTransaction();
}

FInputRayHit UMeshBoundaryToolBase::BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos)
{
	FHitResult OutHit;
	if (SelectionMechanic->TopologyHitTest(PressPos.WorldRay, OutHit))
	{
		return FInputRayHit(OutHit.Distance);
	}
	return FInputRayHit();
}

bool UMeshBoundaryToolBase::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	SelectionMechanic->UpdateHighlight(DevicePos.WorldRay);
	return true;
}

void UMeshBoundaryToolBase::OnEndHover()
{
	SelectionMechanic->ClearHighlight();
}

#undef LOCTEXT_NAMESPACE
