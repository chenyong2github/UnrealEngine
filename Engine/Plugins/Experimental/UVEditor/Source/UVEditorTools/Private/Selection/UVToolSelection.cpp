// Copyright Epic Games, Inc. All Rights Reserved.

#include "Selection/UVToolSelection.h"

#define LOCTEXT_NAMESPACE "UUVToolSelection"

using namespace UE::Geometry;

void FUVToolSelection::SaveStableEdgeIdentifiers(const FDynamicMesh3& Mesh)
{
	if (Type == EType::Edge)
	{
		StableEdgeIDs.InitializeFromEdgeIDs(Mesh, SelectedIDs);
	}
}

void FUVToolSelection::RestoreFromStableEdgeIdentifiers(const FDynamicMesh3& Mesh)
{
	if (Type == EType::Edge)
	{
		StableEdgeIDs.GetEdgeIDs(Mesh, SelectedIDs);
	}
}

bool FUVToolSelection::AreElementsPresentInMesh(FDynamicMesh3& Mesh) const
{
	switch (Type)
	{
	case EType::Vertex:
		for (int32 Vid : SelectedIDs)
		{
			if (!Mesh.IsVertex(Vid))
			{
				return false;
			}
		}
		break;
	case EType::Edge:
		for (int32 Eid : SelectedIDs)
		{
			if (!Mesh.IsEdge(Eid))
			{
				return false;
			}
		}
		break;
	case EType::Triangle:
		for (int32 Tid : SelectedIDs)
		{
			if (!Mesh.IsTriangle(Tid))
			{
				return false;
			}
		}
		break;
	}
	return true;
}

#undef LOCTEXT_NAMESPACE