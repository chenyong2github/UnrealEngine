// Copyright Epic Games, Inc. All Rights Reserved.

#include "DynamicMeshAttributeSet.h"




void FDynamicMeshAttributeSet::EnableMaterialID()
{
	if (HasMaterialID() == false)
	{
		MaterialIDAttrib = MakeUnique<FDynamicMeshMaterialAttribute>(ParentMesh);
		MaterialIDAttrib->Initialize((int32)0);
	}
}

void FDynamicMeshAttributeSet::DisableMaterialID()
{
	MaterialIDAttrib.Reset();
}



bool FDynamicMeshAttributeSet::IsSeamEdge(int eid) const
{
	for (const FDynamicMeshUVOverlay& UVLayer : UVLayers)
	{
		if (UVLayer.IsSeamEdge(eid))
		{
			return true;
		}
	}
	return Normals0.IsSeamEdge(eid);
}


bool FDynamicMeshAttributeSet::IsSeamVertex(int VID, bool bBoundaryIsSeam) const
{
	for (const FDynamicMeshUVOverlay& UVLayer : UVLayers)
	{
		if (UVLayer.IsSeamVertex(VID, bBoundaryIsSeam))
		{
			return true;
		}
	}
	return Normals0.IsSeamVertex(VID, bBoundaryIsSeam);
}


void FDynamicMeshAttributeSet::OnNewVertex(int VertexID, bool bInserted)
{
	FDynamicMeshAttributeSetBase::OnNewVertex(VertexID, bInserted);
}


void FDynamicMeshAttributeSet::OnRemoveVertex(int VertexID)
{
	FDynamicMeshAttributeSetBase::OnRemoveVertex(VertexID);
}


void FDynamicMeshAttributeSet::OnNewTriangle(int TriangleID, bool bInserted)
{
	FDynamicMeshAttributeSetBase::OnNewTriangle(TriangleID, bInserted);

	for (FDynamicMeshUVOverlay& UVLayer : UVLayers)
	{
		UVLayer.InitializeNewTriangle(TriangleID);
	}
	Normals0.InitializeNewTriangle(TriangleID);

	if (MaterialIDAttrib)
	{
		int NewValue = 0;
		MaterialIDAttrib->SetNewValue(TriangleID, &NewValue);
	}
}


void FDynamicMeshAttributeSet::OnRemoveTriangle(int TriangleID)
{
	FDynamicMeshAttributeSetBase::OnRemoveTriangle(TriangleID);

	for (FDynamicMeshUVOverlay& UVLayer : UVLayers)
	{
		UVLayer.OnRemoveTriangle(TriangleID);
	}
	Normals0.OnRemoveTriangle(TriangleID);

	// has no effect on MaterialIDAttrib
}

void FDynamicMeshAttributeSet::OnReverseTriOrientation(int TriangleID)
{
	FDynamicMeshAttributeSetBase::OnReverseTriOrientation(TriangleID);

	for (FDynamicMeshUVOverlay& UVLayer : UVLayers)
	{
		UVLayer.OnReverseTriOrientation(TriangleID);
	}
	Normals0.OnReverseTriOrientation(TriangleID);

	// has no effect on MaterialIDAttrib
}

void FDynamicMeshAttributeSet::OnSplitEdge(const FDynamicMesh3::FEdgeSplitInfo& SplitInfo)
{
	FDynamicMeshAttributeSetBase::OnSplitEdge(SplitInfo);

	for (FDynamicMeshUVOverlay& UVLayer : UVLayers)
	{
		UVLayer.OnSplitEdge(SplitInfo);
	}
	Normals0.OnSplitEdge(SplitInfo);

	if (MaterialIDAttrib)
	{
		MaterialIDAttrib->OnSplitEdge(SplitInfo);
	}
}

void FDynamicMeshAttributeSet::OnFlipEdge(const FDynamicMesh3::FEdgeFlipInfo & flipInfo)
{
	FDynamicMeshAttributeSetBase::OnFlipEdge(flipInfo);

	for (FDynamicMeshUVOverlay& UVLayer : UVLayers)
	{
		UVLayer.OnFlipEdge(flipInfo);
	}
	Normals0.OnFlipEdge(flipInfo);

	if (MaterialIDAttrib)
	{
		MaterialIDAttrib->OnFlipEdge(flipInfo);
	}
}


void FDynamicMeshAttributeSet::OnCollapseEdge(const FDynamicMesh3::FEdgeCollapseInfo & collapseInfo)
{
	FDynamicMeshAttributeSetBase::OnCollapseEdge(collapseInfo);

	for (FDynamicMeshUVOverlay& UVLayer : UVLayers)
	{
		UVLayer.OnCollapseEdge(collapseInfo);
	}
	Normals0.OnCollapseEdge(collapseInfo);

	if (MaterialIDAttrib)
	{
		MaterialIDAttrib->OnCollapseEdge(collapseInfo);
	}
}

void FDynamicMeshAttributeSet::OnPokeTriangle(const FDynamicMesh3::FPokeTriangleInfo & pokeInfo)
{
	FDynamicMeshAttributeSetBase::OnPokeTriangle(pokeInfo);

	for (FDynamicMeshUVOverlay& UVLayer : UVLayers)
	{
		UVLayer.OnPokeTriangle(pokeInfo);
	}
	Normals0.OnPokeTriangle(pokeInfo);

	if (MaterialIDAttrib)
	{
		MaterialIDAttrib->OnPokeTriangle(pokeInfo);
	}
}

void FDynamicMeshAttributeSet::OnMergeEdges(const FDynamicMesh3::FMergeEdgesInfo & mergeInfo)
{
	FDynamicMeshAttributeSetBase::OnMergeEdges(mergeInfo);

	for (FDynamicMeshUVOverlay& UVLayer : UVLayers)
	{
		UVLayer.OnMergeEdges(mergeInfo);
	}
	Normals0.OnMergeEdges(mergeInfo);

	if (MaterialIDAttrib)
	{
		MaterialIDAttrib->OnMergeEdges(mergeInfo);
	}
}

void FDynamicMeshAttributeSet::OnSplitVertex(const DynamicMeshInfo::FVertexSplitInfo& SplitInfo, const TArrayView<const int>& TrianglesToUpdate)
{
	FDynamicMeshAttributeSetBase::OnSplitVertex(SplitInfo, TrianglesToUpdate);

	for (FDynamicMeshUVOverlay& UVLayer : UVLayers)
	{
		UVLayer.OnSplitVertex(SplitInfo, TrianglesToUpdate);
	}
	Normals0.OnSplitVertex(SplitInfo, TrianglesToUpdate);

	if (MaterialIDAttrib)
	{
		MaterialIDAttrib->OnSplitVertex(SplitInfo, TrianglesToUpdate);
	}
}

