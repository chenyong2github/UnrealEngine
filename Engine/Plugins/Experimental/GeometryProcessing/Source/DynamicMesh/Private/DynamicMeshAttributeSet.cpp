// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DynamicMeshAttributeSet.h"




void FDynamicMeshAttributeSet::EnableMaterialID()
{
	if (HasMaterialID() == false)
	{
		MaterialIDAttrib = MakeUnique<FDynamicMeshMaterialAttribute>(ParentMesh);
		MaterialIDAttrib->Initialize((int32)0);
	}
}



bool FDynamicMeshAttributeSet::IsSeamEdge(int eid) const
{
	return UV0.IsSeamEdge(eid) || Normals0.IsSeamEdge(eid);
}



void FDynamicMeshAttributeSet::OnNewTriangle(int TriangleID, bool bInserted)
{
	UV0.InitializeNewTriangle(TriangleID);
	Normals0.InitializeNewTriangle(TriangleID);

	if (MaterialIDAttrib)
	{
		int NewValue = 0;
		MaterialIDAttrib->SetNewValue(TriangleID, &NewValue);
	}
}

void FDynamicMeshAttributeSet::OnRemoveTriangle(int TriangleID, bool bRemoveIsolatedVertices)
{
	UV0.OnRemoveTriangle(TriangleID, bRemoveIsolatedVertices);
	Normals0.OnRemoveTriangle(TriangleID, bRemoveIsolatedVertices);

	// has no effect on MaterialIDAttrib
}

void FDynamicMeshAttributeSet::OnReverseTriOrientation(int TriangleID)
{
	UV0.OnReverseTriOrientation(TriangleID);
	Normals0.OnReverseTriOrientation(TriangleID);

	// has no effect on MaterialIDAttrib
}

void FDynamicMeshAttributeSet::OnSplitEdge(const FDynamicMesh3::FEdgeSplitInfo & splitInfo)
{
	UV0.OnSplitEdge(splitInfo);
	Normals0.OnSplitEdge(splitInfo);

	if (MaterialIDAttrib)
	{
		MaterialIDAttrib->OnSplitEdge(splitInfo);
	}
}

void FDynamicMeshAttributeSet::OnFlipEdge(const FDynamicMesh3::FEdgeFlipInfo & flipInfo)
{
	UV0.OnFlipEdge(flipInfo);
	Normals0.OnFlipEdge(flipInfo);

	if (MaterialIDAttrib)
	{
		MaterialIDAttrib->OnFlipEdge(flipInfo);
	}
}


void FDynamicMeshAttributeSet::OnCollapseEdge(const FDynamicMesh3::FEdgeCollapseInfo & collapseInfo)
{
	UV0.OnCollapseEdge(collapseInfo);
	Normals0.OnCollapseEdge(collapseInfo);

	if (MaterialIDAttrib)
	{
		MaterialIDAttrib->OnCollapseEdge(collapseInfo);
	}
}

void FDynamicMeshAttributeSet::OnPokeTriangle(const FDynamicMesh3::FPokeTriangleInfo & pokeInfo)
{
	UV0.OnPokeTriangle(pokeInfo);
	Normals0.OnPokeTriangle(pokeInfo);

	if (MaterialIDAttrib)
	{
		MaterialIDAttrib->OnPokeTriangle(pokeInfo);
	}
}

void FDynamicMeshAttributeSet::OnMergeEdges(const FDynamicMesh3::FMergeEdgesInfo & mergeInfo)
{
	UV0.OnMergeEdges(mergeInfo);
	Normals0.OnMergeEdges(mergeInfo);

	if (MaterialIDAttrib)
	{
		MaterialIDAttrib->OnMergeEdges(mergeInfo);
	}
}

