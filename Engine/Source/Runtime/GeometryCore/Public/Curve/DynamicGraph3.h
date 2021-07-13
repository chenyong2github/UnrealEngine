// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "BoxTypes.h"
#include "DynamicGraph.h"
#include "SegmentTypes.h"
#include "Util/DynamicVector.h"
#include "Util/IndexUtil.h"
#include "Util/IteratorUtil.h"
#include "Util/RefCountVector.h"
#include "Util/SmallListSet.h"
#include "VectorTypes.h"
#include "VectorUtil.h"

namespace UE
{
namespace Geometry
{

template <typename T>
class FDynamicGraph3 : public FDynamicGraph
{
	TDynamicVectorN<T, 3> Vertices;

public:
	static FVector3<T> InvalidVertex()
	{
		return FVector3<T>(TNumericLimits<T>::Max(), 0, 0);
	}

	FVector3<T> GetVertex(int VID) const
	{
		return vertices_refcount.IsValid(VID) ? Vertices.AsVector3(VID) : InvalidVertex();
	}

	void SetVertex(int VID, FVector3<T> VNewPos)
	{
		check(VectorUtil::IsFinite(VNewPos)); // this will really catch a lot of bugs...
		if (vertices_refcount.IsValid(VID))
		{
			Vertices.SetVector3(VID, VNewPos);
			updateTimeStamp(true);
		}
	}

	using FDynamicGraph::GetEdgeV;
	bool GetEdgeV(int EID, FVector3<T>& A, FVector3<T>& B) const
	{
		if (edges_refcount.IsValid(EID))
		{
			A = Vertices.AsVector3(edges[EID].A);
			B = Vertices.AsVector3(edges[EID].B);
			return true;
		}
		return false;
	}

	TSegment3<T> GetEdgeSegment(int EID) const
	{
		checkfSlow(edges_refcount.IsValid(EID), TEXT("FDynamicGraph2.GetEdgeSegment: invalid segment with id %d"), EID);
		const FEdge& e = edges[EID];
		return TSegment3<T>(
			Vertices.AsVector3(e.A),
			Vertices.AsVector3(e.B));
	}

	FVector3<T> GetEdgeCenter(int EID) const
	{
		checkfSlow(edges_refcount.IsValid(EID), TEXT("FDynamicGraph3.GetEdgeCenter: invalid segment with id %d"), EID);
		const FEdge& e = edges[EID];
		return 0.5 * (Vertices.AsVector3(e.A) + Vertices.AsVector3(e.B));
	}

	int AppendVertex(FVector3<T> V)
	{
		int vid = append_vertex_internal();
		Vertices.InsertAt({{V.X, V.Y, V.Z}}, vid);
		return vid;
	}

	FRefCountVector::IndexEnumerable VertexIndicesItr() const
	{
		return vertices_refcount.Indices();
	}

	/** Enumerate positions of all Vertices in graph */
	value_iteration<FVector3<T>> VerticesItr() const
	{
		return vertices_refcount.MappedIndices<FVector3<T>>(
			[=](int vid) {
				return Vertices.template AsVector3<T>(vid);
			});
	}


	// compute vertex bounding box
	FAxisAlignedBox2d GetBounds() const
	{
		TAxisAlignedBox2<T> AABB;
		for (const FVector3<T>& V : Vertices())
		{
			AABB.Contain(V);
		}
		return AABB;
	}


protected:
	// internal used in SplitEdge
	virtual int append_new_split_vertex(int A, int B) override
	{
		FVector3<T> vNew = 0.5 * (GetVertex(A) + GetVertex(B));
		int f = AppendVertex(vNew);
		return f;
	}

	virtual void subclass_validity_checks(TFunction<void(bool)> CheckOrFailF) const override
	{
		for (int VID : VertexIndices())
		{
			FVector3<T> V = GetVertex(VID);
			CheckOrFailF(VectorUtil::IsFinite(V));
		}
	}
};

typedef FDynamicGraph3<double> FDynamicGraph3d;


} // end namespace UE::Geometry
} // end namespace UE