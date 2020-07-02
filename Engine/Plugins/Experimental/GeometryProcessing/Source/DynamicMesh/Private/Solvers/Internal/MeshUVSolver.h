// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh3.h"
#include "Solvers/MatrixInterfaces.h"
#include "Solvers/MeshLaplacian.h"
#include "Solvers/MeshLinearization.h"
#include "Solvers/ConstrainedMeshSolver.h"

#include "MatrixBase.h"

enum class EUVSolveType
{
	NaturalConformal
};

/**
 * FConstrainedMeshUVSolver is an implmentation of IConstrainedMeshUVSolver that solves
 * for UVs using various methods
 *
 */
class DYNAMICMESH_API FConstrainedMeshUVSolver : public UE::Solvers::IConstrainedMeshUVSolver
{
public:
	typedef UE::Solvers::FUVConstraint  FUVConstraint;

	FConstrainedMeshUVSolver(const FDynamicMesh3& DynamicMesh, EUVSolveType UVSolveType);
	virtual ~FConstrainedMeshUVSolver();

	// Add constraint associated with given vertex id.  Boundary vertices will be ignored
	void AddConstraint(const int32 VtxId, const double Weight, const FVector2d& Pos, const bool bPostFix) override;

	// Update the position of an existing constraint.  Bool return if a corresponding constraint weight exists. Boundary vertices will be ignored (and return false).  
	bool UpdateConstraintPosition(const int32 VtxId, const FVector2d& Position, const bool bPostFix) override;

	// The underlying solver will have to refactor the matrix if this is done. Bool return if a corresponding constraint position exists. Boundary vertices will be ignored (and return false).  
	bool UpdateConstraintWeight(const int32 VtxId, const double Weight) override;

	// Clear all constraints associated with this smoother
	void ClearConstraints() override;

	// Test if for constraint associated with given vertex id. Will return false for any boundary vert.
	bool IsConstrained(const int32 VtxId) const override;

	virtual bool SolveUVs(const FDynamicMesh3* DynamicMesh, TArray<FVector2d>& UVBuffer) override;


protected:

	// what type of UV solve to do
	EUVSolveType UVSolveType;

	// The Key (int32) here is the vertex index not vertex ID
	// making it the same as the matrix row
	TMap<int32, FUVConstraint> ConstraintMap;

	// currently unused
	bool bConstraintPositionsDirty = true;
	bool bConstraintWeightsDirty = true;

	// Used to map between VtxId and vertex Index in linear vector..
	FVertexLinearization  VtxLinearization;
};