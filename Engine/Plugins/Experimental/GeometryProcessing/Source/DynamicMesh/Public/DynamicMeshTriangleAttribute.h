// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DynamicMesh3.h"

/**
 * TDynamicMeshTriangleAttribute is an add-on to a FDynamicMesh3 that allows for 
 * per-triangle storage of an attribute value.
 *
 * The FDynamicMesh3 mesh topology operations (eg split/flip/collapse edge, poke face, etc)
 * can be mirrored to the overlay via OnSplitEdge(), etc.
 */
template<typename AttribValueType, int AttribDimension>
class TDynamicMeshTriangleAttribute
{

protected:
	/** The parent mesh this overlay belongs to */
	FDynamicMesh3* ParentMesh;

	/** List of per-triangle attribute values */
	TDynamicVector<AttribValueType> AttribValues;

	friend class FDynamicMesh3;
	friend class FDynamicMeshAttributeSet;

public:
	/** Create an empty overlay */
	TDynamicMeshTriangleAttribute()
	{
		ParentMesh = nullptr;
	}

	/** Create an overlay for the given parent mesh */
	TDynamicMeshTriangleAttribute(FDynamicMesh3* ParentMeshIn)
	{
		ParentMesh = ParentMeshIn;
	}

	/** @return the parent mesh for this overlay */
	const FDynamicMesh3* GetParentMesh() const { return ParentMesh; }
	/** @return the parent mesh for this overlay */
	FDynamicMesh3* GetParentMesh() { return ParentMesh; }

	/** Set this overlay to contain the same arrays as the copy overlay */
	void Copy(const TDynamicMeshTriangleAttribute<AttribValueType, AttribDimension>& Copy)
	{
		AttribValues = Copy.AttribValues;
	}

	/** Initialize the attribute values to the given max triangle ID */
	void Initialize(AttribValueType InitialValue = (AttribValueType)0)
	{
		check(ParentMesh != nullptr);
		AttribValues.Resize(ParentMesh->MaxTriangleID() * AttribDimension);
		AttribValues.Fill(InitialValue);
	}

	void SetNewValue(int NewTriangleID, const AttribValueType* Data)
	{
		int k = NewTriangleID * AttribDimension;
		for (int i = 0; i < AttribDimension; ++i)
		{
			AttribValues.InsertAt(Data[i], k + i);
		}
	}



	//
	// Accessors/Queries
	//  

	/** Get the element at a given index */
	inline void GetValue(int TriangleID, AttribValueType* Data) const
	{
		int k = TriangleID * AttribDimension;
		for (int i = 0; i < AttribDimension; ++i)
		{
			Data[i] = AttribValues[k + i];
		}
	}

	/** Get the element at a given index */
	template<typename AsType>
	void GetValue(int TriangleID, AsType& Data) const
	{
		int k = TriangleID * AttribDimension;
		for (int i = 0; i < AttribDimension; ++i)
		{
			Data[i] = AttribValues[k + i];
		}
	}


	/** Set the element at a given index */
	inline void SetValue(int TriangleID, const AttribValueType* Data)
	{
		int k = TriangleID * AttribDimension;
		for (int i = 0; i < AttribDimension; ++i)
		{
			AttribValues[k + i] = Data[i];
		}
	}

	/** Set the element at a given index */
	template<typename AsType>
	void SetValue(int TriangleID, const AsType& Data)
	{
		int k = TriangleID * AttribDimension;
		for (int i = 0; i < AttribDimension; ++i)
		{
			AttribValues[k + i] = Data[i];
		}
	}

	/**
	 * Copy the attribute value at FromTriangleID to ToTriangleID
	 */
	inline void CopyValue(int FromTriangleID, int ToTriangleID)
	{
		int kA = FromTriangleID * AttribDimension;
		int kB = ToTriangleID * AttribDimension;
		for (int i = 0; i < AttribDimension; ++i)
		{
			AttribValues.InsertAt(AttribValues[kA+i], kB+i);
		}
	}


	/** Returns true if the parent-mesh edge is a "Seam" in this overlay */
	bool IsBorderEdge(int EdgeID, bool bMeshBoundaryIsBorder = true) const
	{
		FIndex2i EdgeTris = ParentMesh->GetEdgeT(EdgeID);
		if (EdgeTris.B == FDynamicMesh3::InvalidID)
		{
			return bMeshBoundaryIsBorder;
		}
		int kA = EdgeTris.A * AttribDimension;
		int kB = EdgeTris.B * AttribDimension;
		for (int i = 0; i < AttribDimension; ++i)
		{
			if (AttribValues[kA+i] != AttribValues[kB+i])
			{
				return true;
			}
		}
	}



public:

	/** Update the overlay to reflect an edge split in the parent mesh */
	void OnSplitEdge(const FDynamicMesh3::FEdgeSplitInfo& SplitInfo)
	{
		CopyValue(SplitInfo.OriginalTriangles.A, SplitInfo.NewTriangles.A);
		if (SplitInfo.OriginalTriangles.B != FDynamicMesh3::InvalidID)
		{
			CopyValue(SplitInfo.OriginalTriangles.B, SplitInfo.NewTriangles.B);
		}
	}

	/** Update the overlay to reflect an edge flip in the parent mesh */
	void OnFlipEdge(const FDynamicMesh3::FEdgeFlipInfo& FlipInfo)
	{
		// yikes! triangles did not actually change so we will leave attrib unmodified
	}

	/** Update the overlay to reflect an edge collapse in the parent mesh */
	void OnCollapseEdge(const FDynamicMesh3::FEdgeCollapseInfo& CollapseInfo)
	{
		// nothing to do here, triangles were only deleted
	}

	/** Update the overlay to reflect a face poke in the parent mesh */
	void OnPokeTriangle(const FDynamicMesh3::FPokeTriangleInfo& PokeInfo)
	{
		CopyValue(PokeInfo.OriginalTriangle, PokeInfo.NewTriangles.A);
		CopyValue(PokeInfo.OriginalTriangle, PokeInfo.NewTriangles.B);
	}

	/** Update the overlay to reflect an edge merge in the parent mesh */
	void OnMergeEdges(const FDynamicMesh3::FMergeEdgesInfo& MergeInfo)
	{
		// nothing to do here because triangles did not change
	}

};

