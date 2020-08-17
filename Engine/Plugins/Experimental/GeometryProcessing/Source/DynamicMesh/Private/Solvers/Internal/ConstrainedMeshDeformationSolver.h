// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh3.h"
#include "Solvers/MatrixInterfaces.h"
#include "Solvers/MeshLaplacian.h"
#include "Solvers/MeshLinearization.h"
#include "Solvers/ConstrainedMeshSolver.h"

#include "MatrixBase.h"


class FConstrainedSolver;

/**
 * FConstrainedMeshDeformationSolver is an implmentation of IConstrainedMeshSolver that solves
 * Mesh Deformation problems by using quadratic energy functions based on the vertex-graph Laplacians. 
 *
 * All constraints are "soft", ie they are included in the system as weighted quadratic energies
 * rather than had constraints.
 *
 * Both the Laplacian weighting scheme/type and The Sparse Matrix Solver type are configurable.
 */
class DYNAMICMESH_API FConstrainedMeshDeformationSolver : public UE::Solvers::IConstrainedMeshSolver
{
public:
	typedef UE::Solvers::FPositionConstraint  FConstraintPosition;


	FConstrainedMeshDeformationSolver(const FDynamicMesh3& DynamicMesh, const ELaplacianWeightScheme Scheme, const EMatrixSolverType MatrixSolverType);
	virtual ~FConstrainedMeshDeformationSolver();

	// Add constraint associated with given vertex id.  Boundary vertices will be ignored
	void AddConstraint(const int32 VtxId, const double Weight, const FVector3d& Pos, const bool bPostFix) override;

	// Update the position of an existing constraint.  Bool return if a corresponding constraint weight exists. Boundary vertices will be ignored (and return false).  
	bool UpdateConstraintPosition(const int32 VtxId, const FVector3d& Position, const bool bPostFix) override;

	// The underlying solver will have to refactor the matrix if this is done. Bool return if a corresponding constraint position exists. Boundary vertices will be ignored (and return false).  
	bool UpdateConstraintWeight(const int32 VtxId, const double Weight) override;

	// Clear all constraints associated with this smoother
	void ClearConstraints() override { FConstrainedMeshDeformationSolver::ClearConstraintPositions(); FConstrainedMeshDeformationSolver::ClearConstraintWeights(); }

	void ClearConstraintWeights() override { ConstraintWeightMap.Empty();    bConstraintWeightsDirty = true; }

	void ClearConstraintPositions() override { ConstraintPositionMap.Empty();  bConstraintPositionsDirty = true; }


	// Test if for constraint associated with given vertex id. Will return false for any boundary vert.
	bool IsConstrained(const int32 VtxId) const override;

	virtual bool Deform(TArray<FVector3d>& PositionBuffer) override { return false; }

	// Sync constraints with internal solver.  If in the process any internal matrix factoring is dirty, it will be rebuilt.
	// Note: this is called from within the Deform() method.   Only call this method if you want to trigger the matrix refactor yourself.
	void UpdateSolverConstraints();



public:
	// these only apply to iterative solvers

	bool SetMaxIterations(int32 MaxIterations);
	bool SetTolerance(double Tol);


protected:


	void ExtractInteriorVertexPositions(const FDynamicMesh3& DynamicMesh, FSOAPositions& Positions) const;

	//void UpdateMeshWithConstraints();
	// Respect any bPostFix constraints by moving those vertices to position defined by said constraint.
	void UpdateWithPostFixConstraints(FSOAPositions& PositionalVector) const;


	// converts the positionalvector to a TArray<FVector3d> where the offset in the array is implicitly the
	// VtxId in the mesh, and not ness the matrix row id
	// NB: the resulting array is treated as sparse and may have un-initialized elements.
	bool CopyInternalPositions(const FSOAPositions& PositionalVector, TArray<FVector3d>& LinearArray) const;

	bool CopyBoundaryPositions(TArray<FVector3d>& LinearArray) const;


protected:

	// The Key (int32) here is the vertex index not vertex ID
	// making it the same as the matrix row
	TMap<int32, FConstraintPosition> ConstraintPositionMap;
	TMap<int32, double>              ConstraintWeightMap;

	bool bConstraintPositionsDirty = true;
	bool bConstraintWeightsDirty = true;

	// Cache the vertex count. boundary + internal
	int32                 VertexCount;

	// Cache the number of internal vertices
	int32                 InternalVertexCount;

	// Used to map between VtxId and vertex Index in linear vector..
	FVertexLinearization  VtxLinearization;

	// Boundary points, split into 3 arrays (x,y,z).
	FSOAPositions         BoundaryPositions;


	// Actual solver that manages the various linear algebra bits.
	TUniquePtr<FConstrainedSolver>         ConstrainedSolver;

	// Sparse Matrix that holds L^T * B where B has the boundary terms.
	FSparseMatrixD                         BoundaryOperator;
};







/**
 * FSoftMeshDeformationSolver is an implmentation of IConstrainedLaplacianMeshSolver that solves
 * Mesh Deformation problems by using quadratic energy functions based on the vertex-graph Laplacians.
 *
 * The primary difference with FConstrainedMeshDeformationSolver is that boundary vertices do not
 * receive special treatment, they are included in the system and solved in the same way. As a result
 * it is generally necessary to add constraints for boundary vertices.
 *
 * All constraints are "soft", ie they are included in the system as weighted quadratic energies
 * rather than had constraints.
 *
 * Voronoi-Area Clamped Cotangent weights are used for the Laplacian, and an LU Sover
 */
class DYNAMICMESH_API FSoftMeshDeformationSolver : public UE::Solvers::IConstrainedLaplacianMeshSolver
{
public:
	typedef UE::Solvers::FPositionConstraint  FPositionConstraint;

	FSoftMeshDeformationSolver(const FDynamicMesh3& DynamicMesh);
	virtual ~FSoftMeshDeformationSolver();

	//
	// IConstrainedMeshSolver API
	//

	// Add constraint associated with given vertex id.  Boundary vertices will be ignored
	void AddConstraint(const int32 VtxId, const double Weight, const FVector3d& Position, const bool bPostFix) override;

	// Update the position of an existing constraint.  Bool return if a corresponding constraint weight exists. Boundary vertices will be ignored (and return false).  
	bool UpdateConstraintPosition(const int32 VtxId, const FVector3d& Position, const bool bPostFix) override;

	// The underlying solver will have to refactor the matrix if this is done. Bool return if a corresponding constraint position exists. Boundary vertices will be ignored (and return false).  
	bool UpdateConstraintWeight(const int32 VtxId, const double Weight) override;

	// Clear all constraints associated with this smoother
	void ClearConstraints() override;

	// do not support these
	void ClearConstraintWeights() override { check(false); }
	void ClearConstraintPositions() override { check(false); }

	// Test if for constraint associated with given vertex id. Will return false for any boundary vert.
	bool IsConstrained(const int32 VtxId) const override;

	virtual bool Deform(TArray<FVector3d>& PositionBuffer) override { return false;	}


	//
	// IConstrainedLaplacianMeshSolver API
	//


	// Update global scale applied to Laplacian vectors before solve.
	virtual void UpdateLaplacianScale(double UniformScale);


public:

	// Sync constraints with internal solver.  If in the process any internal matrix factoring is dirty, it will be rebuilt.
	// Note: this is called from within the Deform() method.   Only call this method if you want to trigger the matrix refactor yourself.
	void UpdateSolverConstraints();


protected:

	// The Key (int32) here is the vertex index not vertex ID
	// making it the same as the matrix row
	TMap<int32, FPositionConstraint> ConstraintMap;

	bool bConstraintPositionsDirty = true;
	bool bConstraintWeightsDirty = true;

	// Used to map between VtxId and vertex Index in linear vector..
	FVertexLinearization  VtxLinearization;

	// Actual solver that manages the various linear algebra bits.
	TUniquePtr<FConstrainedSolver> ConstrainedSolver;

	double LaplacianScale = 1.0;
	bool HasLaplacianScale() const;
	double GetLaplacianScale(int32 LinearVtxIndex) const;
};