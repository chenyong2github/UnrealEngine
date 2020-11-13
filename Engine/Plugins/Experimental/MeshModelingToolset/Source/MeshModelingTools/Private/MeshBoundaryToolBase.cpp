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
		[this]() { return &MeshSpatial; }
	);
	SelectionMechanic->OnSelectionChanged.AddUObject(this, &UMeshBoundaryToolBase::OnSelectionChanged);
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

#undef LOCTEXT_NAMESPACE
