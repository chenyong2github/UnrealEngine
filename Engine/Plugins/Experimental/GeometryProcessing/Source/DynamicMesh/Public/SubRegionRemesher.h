// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Remesher.h"
#include "Util/BufferUtil.h"

/**
 * FSubRegionRemesher is an extension
 */
class FSubRegionRemesher : public FRemesher
{
protected:
	// temp buffer of edges connected to VertexROI
	TSet<int> EdgeROI;
	// list of edges to consider during current remesh pass (not updated during pass)
	TArray<int> Edges;
	// current edge we are at in StartEdges/GetNextEdge iteration
	int CurEdge;

public:
	FSubRegionRemesher(FDynamicMesh3* Mesh) : FRemesher(Mesh)
	{
		VertexControlF = [this](int vid) {
			return this->VertexFilter(vid);
		};
	}

	/**
	 * Set of vertices in ROI. You add vertices here initially, then 
	 * we will update the list during each Remesh pass
	 */
	TSet<int> VertexROI;


	/**
	 * Must call UpdateROI before each remesh pass.
	 */
	void UpdateROI()
	{
		EdgeROI.Reset();
		for (int VertIdx : VertexROI)
		{
			for (int eid : GetMesh()->VtxEdgesItr(VertIdx))
			{
				EdgeROI.Add(eid);
			}
		}
		Edges.Reset();
		BufferUtil::AppendElements(Edges, EdgeROI);
		//Edges = std::vector<int>(EdgeROI.begin(), EdgeROI.end());
	}



	//
	// specialization of Remesher functionality
	// 
protected:
	virtual int StartEdges() override
	{
		CurEdge = 0;
		return (Edges.Num() > 0) ? Edges[CurEdge] : IndexConstants::InvalidID;
	}

	virtual int GetNextEdge(int CurEdgeID, bool& bDone) override
	{
		CurEdge++;
		if (CurEdge >= Edges.Num())
		{
			bDone = true;
			return -1;
		}
		else
		{
			bDone = false;
			return Edges[CurEdge];
		}
	}

	virtual void OnEdgeSplit(int EdgeID, int VertexA, int VertexB, const FDynamicMesh3::FEdgeSplitInfo& SplitInfo) override
	{
		VertexROI.Add(SplitInfo.NewVertex);
	}

	virtual void OnEdgeCollapse(int EdgeID, int VertexA, int VertexB, const FDynamicMesh3::FEdgeCollapseInfo& CollapseInfo) override
	{
		VertexROI.Remove(CollapseInfo.RemovedVertex);
	}

	EVertexControl VertexFilter(int VertexID)
	{
		return (VertexROI.Contains(VertexID) == false) ? EVertexControl::NoMovement : EVertexControl::AllowAll;
	}

};
