// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LaplacianMeshSmoother.h"

#include "FSOAPositions.h"

#include "LaplacianOperators.h"
#include "MatrixSolver.h"
#include "MeshSmoothingUtilities.h"

#include "ProfilingDebugging/ScopedTimers.h"
#include "Async/ParallelFor.h"


DEFINE_LOG_CATEGORY_STATIC(LogMeshSmoother, Log, All);


double ComputeDistSqrd(const FSOAPositions& VecA, const FSOAPositions& VecB)
{
	const int32 NumA = VecA.Num();
	const int32 NumB = VecB.Num();
	checkSlow(NumA == NumB);

#if 0
	// the eigne way?
	double Tmp  = (VecA.Array(0) - VecB.Array(0)).dot(VecA.Array(0) - VecB.Array(0))
				+ (VecA.Array(1) - VecB.Array(1)).dot(VecA.Array(1) - VecB.Array(1))
				+ (VecA.Array(2) - VecB.Array(2)).dot(VecA.Array(2) - VecB.Array(2));
		     
#endif

	// doing it by hand so we can break when the error is large
	double DistSqrd = 0.;
	{
		const auto& AX = VecA.Array(0);
		const auto& AY = VecA.Array(1);
		const auto& AZ = VecA.Array(2);

		const auto& BX = VecB.Array(0);
		const auto& BY = VecB.Array(1);
		const auto& BZ = VecB.Array(2);


		for (int32 i = 0; i < NumA; ++i)
		{
			double TmpX = AX(i)-BX(i);
			double TmpY = AY(i)-BY(i);
			double TmpZ = AZ(i)-BZ(i);

			TmpX *= TmpX;
			TmpY *= TmpY;
			TmpZ *= TmpZ;
			double TmpT = TmpX + TmpY + TmpZ;
			DistSqrd += TmpT;
		}
	}
	return DistSqrd;
}



FConstrainedMeshOperator::FConstrainedMeshOperator(const FDynamicMesh3& DynamicMesh, const ELaplacianWeightScheme Scheme, const EMatrixSolverType MatrixSolverType)
	: VertexCount(DynamicMesh.VertexCount())
{

	Laplacian = ConstructLaplacian(Scheme, DynamicMesh, VtxLinearization, &EdgeVerts);
	FSparseMatrixD& LMatrix = *(Laplacian);

	TUniquePtr<FSparseMatrixD> LTL(new FSparseMatrixD(Laplacian->rows(), Laplacian->cols()));
	FSparseMatrixD& LTLMatrix = *(LTL);

	bool bIsLaplacianSymmetric = (Scheme == ELaplacianWeightScheme::Valence || Scheme == ELaplacianWeightScheme::Uniform);

	if (bIsLaplacianSymmetric)
	{
		// Laplacian is symmetric, i.e. equal to its transpose
		LTLMatrix = LMatrix * LMatrix;
	
		ConstrainedSolver.Reset(new FConstrainedSolver(LTL, MatrixSolverType));
	}
	else
	{
		// the laplacian 
		LTLMatrix = LMatrix.transpose() * LMatrix;
		ConstrainedSolver.Reset(new FConstrainedSolver(LTL, MatrixSolverType));
	}

}

void FConstrainedMeshOperator::AddConstraint(const int32 VtxId, const double Weight, const FVector3d& Pos, const bool bPostFix)
{

	bConstraintPositionsDirty = true;
	bConstraintWeightsDirty   = true;
	int32 Index = VtxLinearization.ToIndex()[VtxId];

	ConstraintPositionMap.Add(TTuple<int32, FConstraintPosition>(Index, FConstraintPosition(Pos, bPostFix)));
	ConstraintWeightMap.Add(TTuple<int32, double>(Index, Weight));

}


bool FConstrainedMeshOperator::UpdateConstraintPosition(const int32 VtxId, const FVector3d& Pos, const bool bPostFix)
{
	bConstraintPositionsDirty = true;
	int32 Index = VtxLinearization.ToIndex()[VtxId];
	// Add should over-write any existing value for this key
	ConstraintPositionMap.Add(TTuple<int32, FConstraintPosition>(Index, FConstraintPosition(Pos, bPostFix)));

	return ConstraintWeightMap.Contains(VtxId);
}

bool FConstrainedMeshOperator::UpdateConstraintWeight(const int32 VtxId, const double Weight)
{

	bConstraintWeightsDirty = true;
	int32 Index = VtxLinearization.ToIndex()[VtxId];
	// Add should over-write any existing value for this key
	ConstraintWeightMap.Add(TTuple<int32, double>(Index, Weight));

	return ConstraintPositionMap.Contains(VtxId);
}


bool FConstrainedMeshOperator::IsConstrained(const int32 VtxId) const
{
	if (VtxId > VtxLinearization.ToIndex().Num()) return false;

	int32 Index = VtxLinearization.ToIndex()[VtxId];

	return ConstraintWeightMap.Contains(Index);
}

void FConstrainedMeshOperator::UpdateSolverConstraints()
{
	if (bConstraintWeightsDirty)
	{
		ConstrainedSolver->SetConstraintWeights(ConstraintWeightMap);
		bConstraintWeightsDirty = false;
	}
	
	if (bConstraintPositionsDirty)
	{
		ConstrainedSolver->SetContraintPositions(ConstraintPositionMap);
		bConstraintPositionsDirty = false;
	}	
}

bool FConstrainedMeshOperator::Linearize(const FSOAPositions& PositionalVector, TArray<FVector3d>& LinearArray) const
{
	// Number of positions

	const int32 Num = PositionalVector.XVector.rows();

	// early out if the x,y,z arrays in the PositionalVector have different lengths
	if (!PositionalVector.bHasSize(Num))
	{
		return false;
	}

	// 
	const auto& IndexToVtxId = VtxLinearization.ToId();
	const int32 MaxVtxId = IndexToVtxId.Num(); // NB: this is really max_used + 1 in the mesh.  See  FDynamicMesh3::MaxVertexID()



	LinearArray.Empty(MaxVtxId);
	LinearArray.AddUninitialized(MaxVtxId);

	for (int32 i = 0; i < Num; ++i)
	{
		const int32 VtxId = IndexToVtxId[i];

		LinearArray[VtxId] = FVector3d(PositionalVector.XVector.coeff(i), PositionalVector.YVector.coeff(i), PositionalVector.ZVector.coeff(i));
	}

	return true;
}

void FConstrainedMeshOperator::ExtractVertexPositions(const FDynamicMesh3& DynamicMesh, FSOAPositions& VertexPositions) const
{
	VertexPositions.SetZero(VertexCount);


	const TArray<int32>& ToIndex = VtxLinearization.ToIndex();


	for (int32 VtxId : DynamicMesh.VertexIndicesItr())
	{
		const int32 i = ToIndex[VtxId];

		checkSlow(i != FDynamicMesh3::InvalidID);

		const FVector3d& Vertex = DynamicMesh.GetVertex(VtxId);
		// coeffRef - coeff access without range checking
		VertexPositions.XVector.coeffRef(i) = Vertex.X;
		VertexPositions.YVector.coeffRef(i) = Vertex.Y;
		VertexPositions.ZVector.coeffRef(i) = Vertex.Z;
	}
}

void FConstrainedMeshOperator::UpdateWithPostFixConstraints(FSOAPositions& PositionVector) const
{
	for (const auto& ConstraintPosition : ConstraintPositionMap)
	{
		const int32 Index = ConstraintPosition.Key;
		const FConstraintPosition& Constraint = ConstraintPosition.Value;

		// we only care about post-fix constraints

		if (Constraint.bPostFix)
		{
			const FVector3d& Pos = Constraint.Position;
			PositionVector.XVector[Index] = Pos.X;
			PositionVector.YVector[Index] = Pos.Y;
			PositionVector.ZVector[Index] = Pos.Z;
		}
	}
}



FConstrainedMeshDeformer::FConstrainedMeshDeformer(const FDynamicMesh3& DynamicMesh, const ELaplacianWeightScheme LaplacianType)
	: FConstrainedMeshOperator(DynamicMesh, LaplacianType, EMatrixSolverType::LU)
	, LaplacianVectors(FConstrainedMeshOperator::VertexCount)
{

	// The current vertex positions 
	
	// Note: the OriginalVertexPositions are being stored as member data 
	// for use if the solver is iterative.
	// FSOAPositions OriginalVertexPositions; 
	ExtractVertexPositions(DynamicMesh, OriginalVertexPositions);
	
	
	// The biharmonic part of the constrained solver
	//   Biharmonic := Laplacian^{T} * Laplacian
	
	const auto& Biharmonic = ConstrainedSolver->Biharmonic();

	// Compute the Laplacian Vectors
	//    := Biharmonic * VertexPostion
	// In the case of the cotangent laplacian this can be identified as the mean curvature * normal.
	for (int32 i = 0; i < 3; ++i)
	{
		LaplacianVectors.Array(i) = Biharmonic * OriginalVertexPositions.Array(i);
	}
}

bool FConstrainedMeshDeformer::Deform(TArray<FVector3d>& PositionBuffer) 
{
	
	// Update constraints.  This only trigger solver rebuild if the weights were updated.
	UpdateSolverConstraints();

	// Allocate space for the result as a struct of arrays
	FSOAPositions SolutionVector(VertexCount);

	// Solve the linear system
	// NB: the original positions will only be used if the underlying solver type is iterative	
	bool bSuccess = ConstrainedSolver->SolveWithGuess(OriginalVertexPositions, LaplacianVectors, SolutionVector);
	
	// Move any vertices to match bPostFix constraints

	UpdateWithPostFixConstraints(SolutionVector);

	// Copy the result into the array of stucts form.  
	// NB: this re-indexes so the results can be looked up using VtxId

	Linearize(SolutionVector, PositionBuffer);

	// the matrix solve state
	return bSuccess;

}


bool FBiHarmonicMeshSmoother::ComputeSmoothedMeshPositions(TArray<FVector3d>& UpdatedPositions)
{

	UpdateSolverConstraints();

	// Solves the constrained system and updates the mesh 

	FSOAPositions SolutionVector(VertexCount);

	bool bSuccess = ConstrainedSolver->Solve(SolutionVector);


	// Move any vertices to match bPostFix constraints

	UpdateWithPostFixConstraints(SolutionVector);

	// Move vertices to solution positions

	Linearize(SolutionVector, UpdatedPositions);

	return bSuccess;
}

bool FCGBiHarmonicMeshSmoother::ComputeSmoothedMeshPositions(TArray<FVector3d>& UpdatedPositions)
{

	UpdateSolverConstraints();

	// Solves the constrained system and updates the mesh 

	// Solves the constrained system

	FSOAPositions SolutionVector(VertexCount);

	bool bSuccess = ConstrainedSolver->Solve(SolutionVector);


	// Move any vertices to match bPostFix constraints

	UpdateWithPostFixConstraints(SolutionVector);

	// Move vertices to solution positions

	Linearize(SolutionVector, UpdatedPositions);

	return bSuccess;
}



FDiffusionIntegrator::FDiffusionIntegrator(const FDynamicMesh3& DynamicMesh, const ELaplacianWeightScheme Scheme)
{
	Id = 0;
	bIsSymmetric = false;
	MinDiagonalValue = 0;

	VertexCount = DynamicMesh.VertexCount();
	
	// Allocate the buffers.
	Tmp[0].SetZero(VertexCount);
	Tmp[1].SetZero(VertexCount);


}

void FDiffusionIntegrator::Initialize(const FDynamicMesh3& DynamicMesh, const ELaplacianWeightScheme Scheme)
{
	// Construct the laplacian, and extract the mapping for vertices (VtxLinearization)
	DiffusionOperator = ConstructDiffusionOperator(Scheme, DynamicMesh, bIsSymmetric, VtxLinearization, &EdgeVerts);

	const auto& ToVertId = VtxLinearization.ToId();

	// Extract current positions.
	for (int32 i = 0; i < VertexCount; ++i)
	{
		int32 VtxId = ToVertId[i];

		const FVector3d Pos = DynamicMesh.GetVertex(VtxId);
		Tmp[0].XVector[i] = Pos.X;
		Tmp[0].YVector[i] = Pos.Y;
		Tmp[0].ZVector[i] = Pos.Z;
	}

	const FSparseMatrixD&  M = *DiffusionOperator;

	// Find the min diagonal entry (all should be negative).
	int32 Rank = M.rows();
	MinDiagonalValue = FSparseMatrixD::Scalar(0);
	for (int32 i = 0; i < Rank; ++i)
	{
		auto Diag = M.coeff(i, i);
		MinDiagonalValue = FMath::Min(Diag, MinDiagonalValue);
	}


#if 0
	// testing - how to print the matrix to debug output 

	std::stringstream  ss;
	ss << Eigen::MatrixXd(M) << std::endl;

	FString Foo = ss.str().c_str();
	FPlatformMisc::LowLevelOutputDebugStringf(*Foo);
#endif

}



void FDiffusionIntegrator::Integrate_ForwardEuler(int32 NumSteps, double Alpha, double)
{
	Alpha = FMath::Clamp(Alpha, 0., 1.);

	const FSparseMatrixD& M = *DiffusionOperator;
	FSparseMatrixD::Scalar TimeStep = -Alpha / MinDiagonalValue;
	Id = 0;
	for (int32 s = 0; s < NumSteps; ++s)
	{

		int32 SrcBuffer = Id;
		Id = 1 - Id;
		Tmp[Id].XVector = Tmp[SrcBuffer].XVector + TimeStep * M * Tmp[SrcBuffer].XVector;
		Tmp[Id].YVector = Tmp[SrcBuffer].YVector + TimeStep * M * Tmp[SrcBuffer].YVector;
		Tmp[Id].ZVector = Tmp[SrcBuffer].ZVector + TimeStep * M * Tmp[SrcBuffer].ZVector;

	}

}



void FDiffusionIntegrator::Integrate_BackwardEuler(const EMatrixSolverType MatrixSolverType, int32 NumSteps, double Alpha, double Intensity)
{

	//typedef typename TMatrixSolverTrait<EMatrixSolverType::LU>::MatrixSolverType   MatrixSolverType;

	// We solve 
	// p^{n+1} - dt * L[p^{n+1}] = p^{n}
	// 
	// i.e.
	// [I - dt * L ] p^{n+1} = p^{n}
	//
	// NB: in the case of the cotangent laplacian this would be better if we broke the L int
	// L = (A^{-1}) H  where A is the "area matrix" (think "mass matrix"), then this would
	// become
	// [A - dt * H] p^{n+1} = Ap^{n}  
	//  
	// A - dt * H would be symmetric
	//


	const FSparseMatrixD& L = *DiffusionOperator;
	// Identity matrix
	FSparseMatrixD Ident(L.rows(), L.cols());
	Ident.setIdentity();

	FSparseMatrixD::Scalar TimeStep = Alpha * FMath::Min(Intensity, 1.e6);
	
	FSparseMatrixD M = Ident -TimeStep * L;

	
	M.makeCompressed();

	TUniquePtr<IMatrixSolverBase> MatrixSolver = ContructMatrixSolver(MatrixSolverType);

	MatrixSolver->SetUp(M, bIsSymmetric);

	if (MatrixSolver->bIsIterative())
	{
		IIterativeMatrixSolverBase* IterativeSolver = (IIterativeMatrixSolverBase*)MatrixSolver.Get();

		bool bForceSingleThreaded = false;
		Id = 0;
		for (int32 s = 0; s < NumSteps; ++s)
		{

			int32 SrcBuffer = Id;
			Id = 1 - Id;

			// Old solution is the guess.
			IterativeSolver->SolveWithGuess(Tmp[SrcBuffer], Tmp[SrcBuffer], Tmp[Id]);

		}
	}
	else
	{
		bool bForceSingleThreaded = false;
		Id = 0;
		for (int32 s = 0; s < NumSteps; ++s)
		{

			int32 SrcBuffer = Id;
			Id = 1 - Id;

			MatrixSolver->Solve(Tmp[SrcBuffer], Tmp[Id]);
		}
	}
	

}

void FDiffusionIntegrator::GetPositions(TArray<FVector3d>& PositionArray) const
{
	Linearize(Tmp[Id], PositionArray);
}

bool FDiffusionIntegrator::Linearize(const FSOAPositions& PositionalVector, TArray<FVector3d>& LinearArray) const
{
	// Number of positions

	const int32 Num = PositionalVector.XVector.rows();

	// early out if the x,y,z arrays in the PositionalVector have different lengths
	if (!PositionalVector.bHasSize(Num))
	{
		return false;
	}

	// 
	const auto& IndexToVtxId = VtxLinearization.ToId();
	const int32 MaxVtxId = IndexToVtxId.Num(); // NB: this is really max_used + 1 in the mesh.  See  FDynamicMesh3::MaxVertexID()



	LinearArray.Empty(MaxVtxId);
	LinearArray.AddUninitialized(MaxVtxId);

	for (int32 i = 0; i < Num; ++i)
	{
		const int32 VtxId = IndexToVtxId[i];

		LinearArray[VtxId] = FVector3d(PositionalVector.XVector.coeff(i), PositionalVector.YVector.coeff(i), PositionalVector.ZVector.coeff(i));
	}

	return true;
}


TUniquePtr<FSparseMatrixD> FLaplacianDiffusionMeshSmoother::ConstructDiffusionOperator( const ELaplacianWeightScheme Scheme,
																						const FDynamicMesh3& DynamicMesh,
																						bool& bIsOperatorSymmetric,
																						FVertexLinearization& Linearization,
																						TArray<int32>* EdgeVtx ) 
{
	bIsOperatorSymmetric = bIsSymmetricLaplacian(Scheme);


	// Construct the laplacian, and extract the mapping for vertices (VtxLinearization)
	TUniquePtr<FSparseMatrixD> Laplacian = ConstructLaplacian(Scheme, DynamicMesh, VtxLinearization, &EdgeVerts);
	Laplacian->makeCompressed();
	return Laplacian;
};

TUniquePtr<FSparseMatrixD> FBiHarmonicDiffusionMeshSmoother::ConstructDiffusionOperator( const ELaplacianWeightScheme Scheme,
																						 const FDynamicMesh3& DynamicMesh,
																						 bool& bIsOperatorSymmetric,
																						 FVertexLinearization& Linearization,
																						 TArray<int32>* EdgeVtx ) 
{
	bIsOperatorSymmetric = true;
	
	// Construct the laplacian, and extract the mapping for vertices (VtxLinearization)
	TUniquePtr<FSparseMatrixD> Laplacian = ConstructLaplacian(Scheme, DynamicMesh, VtxLinearization, &EdgeVerts);
	const FSparseMatrixD& L = *Laplacian;
	TUniquePtr<FSparseMatrixD> MatrixOperator(new FSparseMatrixD());
	
	bool bIsLaplacianSymmetric = bIsSymmetricLaplacian(Scheme);

	if (bIsLaplacianSymmetric)
	{
		
		*MatrixOperator = -1. * L * L;
	}
	else
	{
		*MatrixOperator = -1. * L.transpose() * L;
	}

	MatrixOperator->makeCompressed();
	return MatrixOperator;
};


void MeshSmoothingOperators::ComputeSmoothing_BiHarmonic(const ELaplacianWeightScheme WeightScheme, const FDynamicMesh3& OriginalMesh, 
	                                                     const double Speed, const double Intensity, const int32 NumIterations, TArray<FVector3d>& PositionArray)
{



	// This is equivalent to taking a single backward Euler time step of bi-harmonic diffusion
	// where L is the Laplacian (Del^2) , and L^T L is an approximation of the Del^4.
	// 
	// dp/dt = - k*k L^T L[p]
	// with 
	// weight = 1 / (k * Sqrt[dt] )
	//
	// p^{n+1} + dt * k * k L^TL [p^{n+1}] = p^{n}
	//
	// re-write as
	// L^TL[p^{n+1}] + weight * weight p^{n+1} = weight * weight p^{n}

#ifndef EIGEN_MPL2_ONLY
	const EMatrixSolverType MatrixSolverType = EMatrixSolverType::LTL;
#else
	// const EMatrixSolverType MatrixSolverType = EMatrixSolverType::LU;
	// const EMatrixSolverType MatrixSolverType = EMatrixSolverType::PCG;

	// The Symmetric Laplacians are SPD, and so are the LtL Operators
	const EMatrixSolverType MatrixSolverType = (bIsSymmetricLaplacian(WeightScheme))? EMatrixSolverType::PCG : EMatrixSolverType::LU;
	
#endif


	FString DebugLogString = FString::Printf(TEXT("Biharmonic Smoothing of mesh with %d verts "), OriginalMesh.VertexCount()) + LaplacianSchemeName(WeightScheme) + MatrixSolverName(MatrixSolverType);

	FScopedDurationTimeLogger Timmer(DebugLogString);

	FBiHarmonicDiffusionMeshSmoother BiHarmonicDiffusionSmoother(OriginalMesh, WeightScheme);

	BiHarmonicDiffusionSmoother.Integrate_BackwardEuler(MatrixSolverType, NumIterations, Speed, Intensity);

	BiHarmonicDiffusionSmoother.GetPositions(PositionArray);


}

void MeshSmoothingOperators::ComputeSmoothing_ImplicitBiHarmonicPCG( const ELaplacianWeightScheme WeightScheme, const FDynamicMesh3& OriginalMesh, 
	                                                                 const double Speed, const double Weight, const int32 MaxIterations, TArray<FVector3d>& PositionArray)
{

	// This is equivalent to taking a single backward Euler time step of bi-harmonic diffusion
	// where L is the Laplacian (Del^2) , and L^T L is an approximation of the Del^4.
	// 
	// dp/dt = - k*k L^T L[p]
	// with 
	// weight = 1 / (k * Sqrt[dt] )
	//
	// p^{n+1} + dt * k * k L^TL [p^{n+1}] = p^{n}
	//
	// re-write as
	// L^TL[p^{n+1}] + weight * weight p^{n+1} = weight * weight p^{n}

	FString DebugLogString = FString::Printf(TEXT("PCG Biharmonic Smoothing of mesh with %d verts "), OriginalMesh.VertexCount()) + LaplacianSchemeName(WeightScheme);

	FScopedDurationTimeLogger Timmer(DebugLogString);

	if (MaxIterations < 1) return;

	FCGBiHarmonicMeshSmoother Smoother(OriginalMesh, WeightScheme);

	// Treat all vertices as constraints with the same weight
	const bool bPostFix = false;

	for (int32 VertId : OriginalMesh.VertexIndicesItr())
	{
		FVector3d Pos = OriginalMesh.GetVertex(VertId);

		Smoother.AddConstraint(VertId, Weight, Pos, bPostFix);
	}

	Smoother.SetMaxIterations(MaxIterations);
	Smoother.SetTolerance(1.e-4);

	bool bSuccess = Smoother.ComputeSmoothedMeshPositions(PositionArray);

}

void  MeshSmoothingOperators::ComputeSmoothing_Diffusion( const ELaplacianWeightScheme WeightScheme, const FDynamicMesh3& OriginalMesh, bool bForwardEuler,
	                                                      const double Speed, const double Intensity, const int32 IterationCount, TArray<FVector3d>& PositionArray)
{

#ifndef EIGEN_MPL2_ONLY
	const EMatrixSolverType MatrixSolverType = EMatrixSolverType::LTL;
#else
	const EMatrixSolverType MatrixSolverType = EMatrixSolverType::LU;
	//const EMatrixSolverType MatrixSolverType = EMatrixSolverType::PCG;
	//const EMatrixSolverType MatrixSolverType = EMatrixSolverType::BICGSTAB;
#endif

	FString DebugLogString = FString::Printf(TEXT("Diffusion Smoothing of mesh with %d verts"), OriginalMesh.VertexCount());
	if (!bForwardEuler)
	{
		DebugLogString += MatrixSolverName(MatrixSolverType);
	}

	FScopedDurationTimeLogger Timmer(DebugLogString);

	if (IterationCount < 1) return;

	FLaplacianDiffusionMeshSmoother Smoother(OriginalMesh, WeightScheme);

	if (bForwardEuler)
	{
		Smoother.Integrate_ForwardEuler(IterationCount, Speed, Intensity);
	}
	else
	{
		Smoother.Integrate_BackwardEuler(MatrixSolverType, IterationCount, Speed, Intensity);
	}

	Smoother.GetPositions(PositionArray);
};


TUniquePtr<MeshDeformingOperators::IConstrainedMeshOperator> MeshDeformingOperators::ConstructConstrainedMeshDeformer(const ELaplacianWeightScheme WeightScheme, const FDynamicMesh3& DynamicMesh)
{
	TUniquePtr<MeshDeformingOperators::IConstrainedMeshOperator> Deformer(new FConstrainedMeshDeformer(DynamicMesh, WeightScheme));

	return Deformer;
}


TUniquePtr<MeshDeformingOperators::IConstrainedMeshOperator> MeshDeformingOperators::ConstructConstrainedMeshSmoother(const ELaplacianWeightScheme WeightScheme, const FDynamicMesh3& DynamicMesh)
{
	TUniquePtr<MeshDeformingOperators::IConstrainedMeshOperator> Deformer(new FBiHarmonicMeshSmoother(DynamicMesh, WeightScheme));

	return Deformer;
}