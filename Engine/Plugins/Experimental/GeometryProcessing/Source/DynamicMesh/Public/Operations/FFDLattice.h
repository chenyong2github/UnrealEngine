// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VectorTypes.h"
#include "BoxTypes.h"
#include "Containers/StaticArray.h"

class FDynamicMesh3;
class FProgressCancel;


enum class ELatticeInterpolation : uint8
{
	Linear = 0,
	Cubic = 1
};

struct FLatticeExecutionInfo
{
	bool bParallel = true;
	int CancelCheckSize = 100000;		// Number of vertices to process before checking for cancel
};

/** 
 * Free-form deformation lattice. Initialize it with a mesh and desired resolution, get the initial lattice points out.
 * Then pass in deformed lattice points and it will compute deformed mesh vertex positions.
 */
class DYNAMICMESH_API FFFDLattice
{
public:

	/// Linear interpolation information. There should be one of these per mesh vertex.
	struct FEmbedding
	{
		FVector3i LatticeCell = { -1, -1, -1 };		// Original point is in the hexahedron whose Min corner is LatticeCell
		FVector3d CellWeighting = { 0,0,0 };	// Original linear interpolation weights of the point in the cell
	};

	/// Create a lattice that fits the given mesh and has the given resolution along each dimension. Also precompute
	/// the weight of each mesh vertex inside the appropriate lattice cell.
	FFFDLattice(const FVector3i& InDims, const FDynamicMesh3& InMesh, float Padding);

	/// Using the Lattice's current control point positions and the original embedding information, compute the
	/// deformed mesh vertex positions.
	void GetDeformedMeshVertexPositions(const TArray<FVector3d>& LatticeControlPoints,
										TArray<FVector3d>& OutVertexPositions, 
										ELatticeInterpolation Interpolation = ELatticeInterpolation::Linear,
										FLatticeExecutionInfo ExecutionInfo = FLatticeExecutionInfo(),
										FProgressCancel* Progress = nullptr) const;

	void GenerateInitialLatticePositions(TArray<FVector3d>& OutLatticePositions) const;
	void GenerateLatticeEdges(TArray<FVector2i>& OutLatticeEdges) const;

protected:

	// Number of lattice points in each dimension
	FVector3i Dims;

	// Extents of the lattice before it is deformed
	FAxisAlignedBox3d InitialBounds;

	// 3D size of a lattice cell
	FVector3d CellSize;

	// Interpolation weights and cell indices per vertex of the input FDynamicMesh3
	TArray<FEmbedding> VertexEmbeddings;


	void InitializeLatticePositions();

	/// Get the index into the flat TArray of positions given the (i,j,k) coordinates in the lattice
	int ControlPointIndexFromCoordinates(int i, int j, int k) const
	{
		int Idx = k + Dims.Z * (j + Dims.Y * i);
		return Idx;
	}

	int ControlPointIndexFromCoordinates(const FVector3i& Index) const
	{
		return ControlPointIndexFromCoordinates(Index.X, Index.Y, Index.Z);
	}

	/// For each vertex in Mesh, compute the lattice cell it resides in and its weighting
	void ComputeInitialEmbedding(const FDynamicMesh3& Mesh, FLatticeExecutionInfo ExecutionInfo = FLatticeExecutionInfo());

	/// Compute cell index and linear interpolation weights for a given point
	FVector3d ComputeTrilinearWeights(const FVector3d& Pt, FVector3i& GridCoordinates) const;

	/// Helper for linear interpolation. Returns lattice points at (I,J,K) and (I+1,J,K).
	void GetValuePair(int I, int J, int K, FVector3d& A, FVector3d& B, const TArray<FVector3d>& LatticeControlPoints) const;

	/// Use the given cell index and weights to compute the interpolated position
	FVector3d InterpolatedPosition(const FEmbedding& VertexEmbedding, const TArray<FVector3d>& LatticeControlPoints) const;
	FVector3d InterpolatedPositionCubic(const FEmbedding& VertexEmbedding, const TArray<FVector3d>& LatticeControlPoints) const;

	/// Clamp the given index to [0, Dims] and return the current lattice point position at the clamped index
	FVector3d ClosestLatticePosition(const FVector3i& VirtualControlPointIndex, const TArray<FVector3d>& LatticeControlPoints) const;

	/// Compute the extrapolated position for a "virtual" lattice control point outside of the actual lattice
	FVector3d ExtrapolatedLatticePosition(const FVector3i& VirtualPointIndex, const TArray<FVector3d>& LatticeControlPoints) const;

};

