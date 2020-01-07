// Copyright Epic Games, Inc. All Rights Reserved.

#include "StaticMeshAdapter.h"

#include "EditableMesh.h"
#include "MeshEditorStaticMeshAdapter.h"
#include "StaticMeshAttributes.h"
#include "WireframeMeshComponent.h"
#include "Engine/StaticMesh.h"

void UStaticMeshEditorStaticMeshAdapter::OnEndModification(const UEditableMesh* EditableMesh)
{
	if (StaticMesh != nullptr && EditableMesh->CurrentModificationType == EMeshModificationType::Final && EditableMesh->CurrentToplogyChange == EMeshTopologyChange::TopologyChange)
	{
		StaticMesh->CommitMeshDescription(LODIndex);
	}
}

void UStaticMeshEditorStaticMeshAdapter::OnRebuildRenderMesh(const UEditableMesh* EditableMesh)
{
	UMeshEditorStaticMeshAdapter::OnRebuildRenderMesh(EditableMesh);

	for( FWireframeEdge& Edge : WireframeMesh->Edges )
	{
		Edge.Color = FColor::Black;
	}
}

void UStaticMeshEditorStaticMeshAdapter::OnCreateEdges(const UEditableMesh* EditableMesh, const TArray<FEdgeID>& EdgeIDs)
{
	UMeshEditorStaticMeshAdapter::OnCreateEdges(EditableMesh, EdgeIDs);

	// Override the edge color like in OnRebuildRenderMesh
	for (FEdgeID EdgeID : EdgeIDs)
	{
		FWireframeEdge& Edge = WireframeMesh->Edges[EdgeID.GetValue()];
		Edge.Color = FColor::Black;
	}
}

void UStaticMeshEditorStaticMeshAdapter::OnSetEdgeAttribute(const UEditableMesh* EditableMesh, const FEdgeID EdgeID, const FMeshElementAttributeData& Attribute)
{
	// Override the edge color like in OnRebuildRenderMesh
	if (Attribute.AttributeName == MeshAttribute::Edge::IsHard)
	{
		WireframeMesh->SetEdgeColor(EdgeID, FColor::Black);
	}
}