// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh3.h"
#include "ConstrainedPoissonSolver.h"
#include "FSOAPositions.h"
#include "MatrixSolver.h"
#include "MeshSmoothingUtilities.h"
#include "MeshElementLinearizations.h"



class MESHSOLVERUTILITIES_API FConstrainedMeshOperator : public MeshDeformingOperators::IConstrainedMeshOperator
{
public:
	typedef FConstrainedSolver::FConstraintPosition   FConstraintPosition;


	FConstrainedMeshOperator(const FDynamicMesh3& DynamicMesh, const ELaplacianWeightScheme Scheme, const EMatrixSolverType MatrixSolverType);
	virtual ~FConstrainedMeshOperator() override {}

	// Add constraint associated with given vertex id.  Boundary vertices will be ignored
	void AddConstraint(const int32 VtxId, const double Weight, const FVector3d& Pos, const bool bPostFix) override;

	// Update the position of an existing constraint.  Bool return if a corresponding constraint weight exists. Boundary vertices will be ignored (and return false).  
	bool UpdateConstraintPosition(const int32 VtxId, const FVector3d& Position, const bool bPostFix) override;

	// The underlying solver will have to refactor the matrix if this is done. Bool return if a corresponding constraint position exists. Boundary vertices will be ignored (and return false).  
	bool UpdateConstraintWeight(const int32 VtxId, const double Weight) override;

	// Clear all constraints associated with this smoother
	void ClearConstraints() override { FConstrainedMeshOperator::ClearConstraintPositions(); FConstrainedMeshOperator::ClearConstraintWeights(); }

	void ClearConstraintWeights() override { ConstraintWeightMap.Empty();    bConstraintWeightsDirty = true; }

	void ClearConstraintPositions() override { ConstraintPositionMap.Empty();  bConstraintPositionsDirty = true; }


	// Test if for constraint associated with given vertex id. Will return false for any boundary vert.
	bool IsConstrained(const int32 VtxId) const override;

	virtual bool Deform(TArray<FVector3d>& PositionBuffer) override { return false;  }

	// Sync constraints with internal solver.  If in the process any internal matrix factoring is dirty, it will be rebuilt.
	// Note: this is called from within the Deform() method.   Only call this method if you want to trigger the matrix refactor yourself.
	void UpdateSolverConstraints();

protected:


	void ExtractInteriorVertexPositions(const FDynamicMesh3& DynamicMesh, FSOAPositions& Positions) const ;

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
	bool bConstraintWeightsDirty   = true;

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


class MESHSOLVERUTILITIES_API FConstrainedMeshDeformer : public FConstrainedMeshOperator
{
public:
	FConstrainedMeshDeformer(const FDynamicMesh3& DynamicMesh, const ELaplacianWeightScheme LaplacianType);
	~FConstrainedMeshDeformer() override {}

	bool Deform(TArray<FVector3d>& PositionBuffer) override;

private:

	FSOAPositions LaplacianVectors;
	FSOAPositions OriginalInteriorPositions;
};


class FBiHarmonicMeshSmoother : public FConstrainedMeshOperator
{
public:
	typedef FConstrainedMeshOperator         MyBaseType;

	FBiHarmonicMeshSmoother(const FDynamicMesh3& DynamicMesh, const ELaplacianWeightScheme Scheme) :
		MyBaseType(DynamicMesh, Scheme, EMatrixSolverType::LU)
	{}

	bool Deform(TArray<FVector3d>& UpdatedPositions) override 
	{
		return	ComputeSmoothedMeshPositions(UpdatedPositions);
	}

	// (Direct) Solve the constrained system and populate the UpdatedPositions with the result 
	bool ComputeSmoothedMeshPositions(TArray<FVector3d>& UpdatedPositions);

};


// NB: This conjugate gradient solver could be updated to use  solveWithGuess() method on the iterative solver
class FCGBiHarmonicMeshSmoother : public FConstrainedMeshOperator
{
public:
	typedef FConstrainedMeshOperator      MyBaseType;

	FCGBiHarmonicMeshSmoother(const FDynamicMesh3& DynamicMesh, const ELaplacianWeightScheme Scheme) :
		MyBaseType(DynamicMesh, Scheme, EMatrixSolverType::BICGSTAB)
	{}

	bool Deform(TArray<FVector3d>& UpdatedPositions) override
	{
		return	ComputeSmoothedMeshPositions(UpdatedPositions);
	}

	void SetMaxIterations(int32 MaxIterations) 
	{ 
		IIterativeMatrixSolverBase* Solver = ConstrainedSolver->GetMatrixSolverIterativeBase(); 
		if (Solver)
		{
			Solver->SetIterations(MaxIterations);
		}
	}

	void SetTolerance(double Tol)
	{
		IIterativeMatrixSolverBase* Solver = ConstrainedSolver->GetMatrixSolverIterativeBase();
		if (Solver)
		{
			Solver->SetTolerance(Tol);
		}
	}

	// (Iterative) Solve the constrained system and populate the UpdatedPositions with the result 
	bool ComputeSmoothedMeshPositions(TArray<FVector3d>& UpdatedPositions);


};


class FDiffusionIntegrator
{
public:

	FDiffusionIntegrator(const FDynamicMesh3& DynamicMesh, const ELaplacianWeightScheme Scheme);
	virtual ~FDiffusionIntegrator() {}

	void Integrate_ForwardEuler(const int32 NumSteps, const double Speed);

	// Note: 
	void Integrate_BackwardEuler(const EMatrixSolverType MatrixSolverType, const int32 NumSteps, const double TimeStepSize);

	void GetPositions(TArray<FVector3d>& PositionArray) const;

protected:


	// The derived class has to implement this.
	// responsible for constructing the Diffusion Operator, the Boundary Operator etc
	virtual void ConstructOperators(const ELaplacianWeightScheme Scheme,
		                            const FDynamicMesh3& Mesh,
	 	                            bool& bIsOperatorSymmetric,
		                            FVertexLinearization& Linearization,
									FSparseMatrixD& DiffusionOp,
									FSparseMatrixD& BoundaryOp ) = 0;

	void Initialize(const FDynamicMesh3& DynamicMesh, const ELaplacianWeightScheme Scheme);

protected:

	// Copy the 
	bool CopyInternalPositions(const FSOAPositions& PositionalVector, TArray<FVector3d>& LinearArray) const;

	bool CopyBoundaryPositions(TArray<FVector3d>& LinearArray) const;

	// Cache the vertex count.
	int32                 VertexCount;

	// Cache the number of internal vertices
	int32                 InternalVertexCount;

	// Used to map between VtxId and vertex Index in linear vector..
	FVertexLinearization  VtxLinearization;


	// I don't know if we want to keep this after the constructor
	bool                                   bIsSymmetric;
	FSparseMatrixD                         DiffusionOperator;
	FSparseMatrixD			               BoundaryOperator;
	TArray<int32>                          EdgeVerts;
	FSparseMatrixD::Scalar                 MinDiagonalValue;
	
	FSOAPositions                       BoundaryPositions;

	FSOAPositions                       Tmp[2];
	int32                               Id; // double buffer id
};


class FLaplacianDiffusionMeshSmoother : public FDiffusionIntegrator
{
public:

	FLaplacianDiffusionMeshSmoother(const FDynamicMesh3& DynamicMesh, const ELaplacianWeightScheme Scheme)
	: FDiffusionIntegrator(DynamicMesh, Scheme)
	{ 
		Initialize(DynamicMesh, Scheme);
	}
	
protected:
	
	void ConstructOperators(const ELaplacianWeightScheme Scheme,
		                    const FDynamicMesh3& Mesh,
		                    bool& bIsOperatorSymmetric,
		                    FVertexLinearization& Linearization,
		                    FSparseMatrixD& DiffusionOp,
		                    FSparseMatrixD& BoundaryOp) override;
};

class  FBiHarmonicDiffusionMeshSmoother : public FDiffusionIntegrator
{
public:

	FBiHarmonicDiffusionMeshSmoother(const FDynamicMesh3& DynamicMesh, const ELaplacianWeightScheme Scheme)
	: FDiffusionIntegrator(DynamicMesh, Scheme)
	{
		Initialize(DynamicMesh, Scheme);
	}

protected:

	void ConstructOperators( const ELaplacianWeightScheme Scheme,
		                     const FDynamicMesh3& Mesh,
		                     bool& bIsOperatorSymmetric,
		                     FVertexLinearization& Linearization,
		                     FSparseMatrixD& DiffusionOp,
		                     FSparseMatrixD& BoundaryOp) override;
};

