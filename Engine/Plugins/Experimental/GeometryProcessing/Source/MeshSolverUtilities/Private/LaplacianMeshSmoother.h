// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh3.h"
#include "ConstrainedPoissonSolver.h"
#include "FSOAPositions.h"
#include "MatrixSolver.h"
#include "MeshSmoothingUtilities.h"
#include "MeshElementLinearizations.h"



class FConstrainedMeshOperator : public MeshDeformingOperators::IConstrainedMeshOperator
{
public:
	typedef FConstrainedSolver::FConstraintPosition   FConstraintPosition;


	FConstrainedMeshOperator(const FDynamicMesh3& DynamicMesh, const ELaplacianWeightScheme Scheme, const EMatrixSolverType MatrixSolverType);
	virtual ~FConstrainedMeshOperator() override {}

	// Add constraint associated with given vertex id.
	void AddConstraint(const int32 VtxId, const double Weight, const FVector3d& Pos, const bool bPostFix) override;


	bool UpdateConstraintPosition(const int32 VtxId, const FVector3d& Position, const bool bPostFix) override;

	// The underlying solver will have to refactor the matrix if this is done
	bool UpdateConstraintWeight(const int32 VtxId, const double Weight) override;

	// Clear all constraints associated with this smoother
	void ClearConstraints() override { FConstrainedMeshOperator::ClearConstraintPositions(); FConstrainedMeshOperator::ClearConstraintWeights(); }

	void ClearConstraintWeights() override { ConstraintWeightMap.Empty();    bConstraintWeightsDirty = true; }

	void ClearConstraintPositions() override { ConstraintPositionMap.Empty();  bConstraintPositionsDirty = true; }


	// Test if for constraint associated with given vertex id.
	bool IsConstrained(const int32 VtxId) const override;

	

	virtual bool Deform(TArray<FVector3d>& PositionBuffer) override { return false;  }

protected:

	// Sync constraints with internal solver.
	void UpdateSolverConstraints();


	void ExtractVertexPositions(const FDynamicMesh3& DynamicMesh, FSOAPositions& Positions) const ;

	//void UpdateMeshWithConstraints();
	// Respect any bPostFix constraints by moving those vertices to position defined by said constraint.
	void UpdateWithPostFixConstraints(FSOAPositions& PositionalVector) const;


	// converts the positionalvector to a TArray<FVector3d> where the offset in the array is implicitly the
	// VtxId in the mesh, and not ness the matrix row id
	// NB: the resulting array is treated as sparse and may have un-initialized elements.
	bool Linearize(const FSOAPositions& PositionalVector, TArray<FVector3d>& LinearArray) const;


protected:

	// The Key (int32) here is the vertex index not vertex ID
	// making it the same as the matrix row
	TMap<int32, FConstraintPosition> ConstraintPositionMap;
	TMap<int32, double>              ConstraintWeightMap;

	bool bConstraintPositionsDirty = true;
	bool bConstraintWeightsDirty   = true;

	// Cache the vertex count.
	int32                 VertexCount;

	// Used to map between VtxId and vertex Index in linear vector..
	FVertexLinearization  VtxLinearization;

	// I don't know if we want to keep this after the constructor
	TUniquePtr<FSparseMatrixD>             Laplacian;
	TArray<int32>                          EdgeVerts;

	// Actual solver that manages the various linear algebra bits.
	TUniquePtr<FConstrainedSolver>         ConstrainedSolver;
};


class FConstrainedMeshDeformer : public FConstrainedMeshOperator
{
public:
	FConstrainedMeshDeformer(const FDynamicMesh3& DynamicMesh, const ELaplacianWeightScheme LaplacianType);
	~FConstrainedMeshDeformer() override {}

	bool Deform(TArray<FVector3d>& PositionBuffer) override;

private:

	FSOAPositions LaplacianVectors;
	FSOAPositions OriginalVertexPositions;
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

	void Integrate_ForwardEuler(int32 NumSteps, double Speed, double);

	// Note: 
	void Integrate_BackwardEuler(const EMatrixSolverType MatrixSolverType, int32 NumSteps, double Speed, double Intensity);

	void GetPositions(TArray<FVector3d>& PositionArray) const;

protected:

	// The derived class has to implement this.
	virtual TUniquePtr<FSparseMatrixD> ConstructDiffusionOperator(const ELaplacianWeightScheme Scheme, 
		                                                          const FDynamicMesh3& Mesh,
		                                                          bool& bIsOperatorSymmetric,
		                                                          FVertexLinearization& Linearization,
		                                                          TArray<int32>* EdgeVtx) = 0 ;

	void Initialize(const FDynamicMesh3& DynamicMesh, const ELaplacianWeightScheme Scheme);

protected:
	bool Linearize(const FSOAPositions& PositionalVector, TArray<FVector3d>& LinearArray) const;

	// Cache the vertex count.
	int32 VertexCount;

	// Used to map vertex ID to entry in linear vector..
	FVertexLinearization  VtxLinearization;

	// I don't know if we want to keep this after the constructor
	bool                                   bIsSymmetric;
	TUniquePtr<FSparseMatrixD>             DiffusionOperator;
	TArray<int32>                          EdgeVerts;
	FSparseMatrixD::Scalar                 MinDiagonalValue;

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
	TUniquePtr<FSparseMatrixD> ConstructDiffusionOperator( const ELaplacianWeightScheme Scheme,
														   const FDynamicMesh3& Mesh,
		                                                   bool& bIsOperatorSymmetric,
		                                                   FVertexLinearization& Linearization,
		                                                   TArray<int32>* EdgeVtx) override ;
	

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
	TUniquePtr<FSparseMatrixD> ConstructDiffusionOperator( const ELaplacianWeightScheme Scheme,
														   const FDynamicMesh3& Mesh,
														   bool& bIsOperatorSymmetric,
														   FVertexLinearization& Linearization,
														   TArray<int32>* EdgeVtx) override;
};

