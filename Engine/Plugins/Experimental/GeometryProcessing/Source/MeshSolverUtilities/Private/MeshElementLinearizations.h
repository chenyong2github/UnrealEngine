// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh3.h"


/**
* Used linearize the VtxIds in a mesh as a single array and allow mapping from array offset to mesh VtxId.
* Generally, the array offset will correspond to a matrix row when forming a Laplacian
*/
class FMeshElementLinearization
{
public:

	FMeshElementLinearization() = default;

	// Lookup   ToVtxId(Index) = VtxId;
	const TArray<int32>& ToId() const { return ToIdMap; }

	// Lookup   ToIndex(VtxId) = Index;  may return FDynamicMesh3::InvalidID 
	const TArray<int32>& ToIndex() const { return ToIndexMap; }

	int32 NumIds() const { return ToIdMap.Num(); }

	// Following the FDynamicMesh3 convention this is really MaxId + 1
	int32 MaxId() const { return ToIndexMap.Num(); }

	void Empty() { ToIdMap.Empty();  ToIndexMap.Empty(); }

	void Populate(const int32 MaxId, const int32 Count, FRefCountVector::IndexEnumerable ElementItr)
	{
		ToIndexMap.SetNumUninitialized(MaxId);
		ToIdMap.SetNumUninitialized(Count);

		for (int32 i = 0; i < MaxId; ++i)
		{
			ToIndexMap[i] = FDynamicMesh3::InvalidID;
		}

		// create the mapping
		{
			int32 N = 0;
			for (int32 Id : ElementItr)
			{
				ToIdMap[N] = Id;
				ToIndexMap[Id] = N;
				N++;
			}
		}
	}

protected:
	TArray<int32>  ToIdMap;
	TArray<int32>  ToIndexMap;

private:
	FMeshElementLinearization(const FMeshElementLinearization&);
};


/**
* Used linearize the VtxIds in a mesh as a single array and allow mapping from array offset to mesh VtxId.
* Generally, the array offset will correspond to a matrix row when forming a Laplacian.
* The last NumBoundaryVerts are the boundary verts.  This may be zero.
*/
class FVertexLinearization : public FMeshElementLinearization
{
public:
	FVertexLinearization() = default;
	FVertexLinearization(const FDynamicMesh3& DynamicMesh)
	{
		Reset(DynamicMesh);
	}

	void Reset(const FDynamicMesh3& DynamicMesh)
	{
		Empty();
		FMeshElementLinearization::Populate(DynamicMesh.MaxVertexID(), DynamicMesh.VertexCount(), DynamicMesh.VertexIndicesItr());
		RemapBoundaryVerts(DynamicMesh);
	}

	int32 NumVerts() const { return FMeshElementLinearization::NumIds(); }

	int32 NumBoundaryVerts() const { return NumBndryVerts; }

private:

	// Moves the boundary verts to the end of the arrays and records the number of boundary verts
	void RemapBoundaryVerts(const FDynamicMesh3& DynamicMesh)
	{
		int32 VertCount = NumVerts();

		// Collect the BoundaryVerts and the internal verts in two array
		TArray<int32> BoundaryVertIds;
		TArray<int32> TmpToIdMap;
		TmpToIdMap.Reserve(VertCount);
		for (int32 i = 0, I = ToIdMap.Num(); i < I; ++i)
		{
			int32 VtxId = ToIdMap[i];
			// Test if the vertex has a one ring
			
			bool bEmptyOneRing = true;
			for (int NeighborVertId : DynamicMesh.VtxVerticesItr(VtxId))
			{
				bEmptyOneRing = false;
				break;
			};
				

			if (bEmptyOneRing || DynamicMesh.IsBoundaryVertex(VtxId))
			{
				BoundaryVertIds.Add(VtxId);
			}
			else
			{
				TmpToIdMap.Add(VtxId);
			}
		}

		// The number of boundary verts
		NumBndryVerts = BoundaryVertIds.Num();

		// Merge the boundary verts at the tail 
		// Add the Boundary verts at the end of the array.
		TmpToIdMap.Append(BoundaryVertIds);
	
		// rebuild the 'to' Index
		for (int32 i = 0, I = ToIndexMap.Num(); i < I; ++i)
		{
			ToIndexMap[i] = FDynamicMesh3::InvalidID;
		}
		for (int32 i = 0, I = TmpToIdMap.Num(); i < I; ++i)
		{
			int32 Id = TmpToIdMap[i];
			ToIndexMap[Id] = i;
		}

		// swap the temp
		Swap(TmpToIdMap, ToIdMap);
	}

private:

	FVertexLinearization(const FVertexLinearization&);

	int32 NumBndryVerts = 0;
};

/**
* Used linearize the TriIds in a mesh as a single array and allow mapping from array offset to mesh TriId.
*
*/
class FTriangleLinearization : public FMeshElementLinearization
{
public:
	FTriangleLinearization() = default;

	FTriangleLinearization(const FDynamicMesh3& DynamicMesh)
	{
		Reset(DynamicMesh);
	}

	void Reset(const FDynamicMesh3& DynamicMesh)
	{
		Empty();
		FMeshElementLinearization::Populate(DynamicMesh.MaxTriangleID(), DynamicMesh.TriangleCount(), DynamicMesh.TriangleIndicesItr());
	}

	int32 NumTris() const { return FMeshElementLinearization::NumIds(); }


private:
	FTriangleLinearization(const FTriangleLinearization&);
};
