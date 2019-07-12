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

	FSparseMatrixD LaplacianInternal;
	FSparseMatrixD LaplacianBoundary;
	ConstructLaplacian(Scheme, DynamicMesh, VtxLinearization, LaplacianInternal, LaplacianBoundary);

	// Copy the original boundary vertex locations
	const int32 BoundaryVertexCount = VtxLinearization.NumBoundaryVerts();
	
	// Number of vertices in the interior of the mesh
	InternalVertexCount = VertexCount - BoundaryVertexCount;

	// Copy the original boundary vertex locations
	BoundaryPositions.Empty(BoundaryVertexCount);
	BoundaryPositions.AddUninitialized(BoundaryVertexCount);
	{
		const auto& ToIdx = VtxLinearization.ToIndex();
		for (int32 i = 0; i < BoundaryVertexCount; ++i)
		{
			int32 VtxId = ToIdx[i + InternalVertexCount];
			FVector3d Pos = DynamicMesh.GetVertex(VtxId);
			BoundaryPositions[i] = Pos;
		}
	}

	checkSlow(LaplacianInternal.rows() == LaplacianInternal.cols());

	TUniquePtr<FSparseMatrixD> LTLPtr( new FSparseMatrixD(LaplacianInternal.rows(), LaplacianInternal.cols()) );
	FSparseMatrixD& LTLMatrix = *(LTLPtr);

	bool bIsLaplacianSymmetric = (Scheme == ELaplacianWeightScheme::Valence || Scheme == ELaplacianWeightScheme::Uniform);

	if (bIsLaplacianSymmetric)
	{
		// Laplacian is symmetric, i.e. equal to its transpose
		LTLMatrix = LaplacianInternal * LaplacianInternal;
	
		ConstrainedSolver.Reset(new FConstrainedSolver(LTLPtr, MatrixSolverType));
	}
	else
	{
		// the laplacian 
		LTLMatrix = LaplacianInternal.transpose() * LaplacianInternal;
		ConstrainedSolver.Reset(new FConstrainedSolver(LTLPtr, MatrixSolverType));
	}

}

void FConstrainedMeshOperator::AddConstraint(const int32 VtxId, const double Weight, const FVector3d& Pos, const bool bPostFix)
{
	const auto& ToIndex = VtxLinearization.ToIndex();
	
	if (VtxId > ToIndex.Num())
	{
		return;
	}

	const int32 Index = ToIndex[VtxId];

	// Only add the constraint if the vertex is actually in the interior.  We aren't solving for edge vertices.
	if (Index != FDynamicMesh3::InvalidID && Index < InternalVertexCount)
	{
		bConstraintPositionsDirty = true;
		bConstraintWeightsDirty = true;


		ConstraintPositionMap.Add(TTuple<int32, FConstraintPosition>(Index, FConstraintPosition(Pos, bPostFix)));
		ConstraintWeightMap.Add(TTuple<int32, double>(Index, Weight));
	}
}


bool FConstrainedMeshOperator::UpdateConstraintPosition(const int32 VtxId, const FVector3d& Pos, const bool bPostFix)
{
	bool Result = false;
	const auto& ToIndex = VtxLinearization.ToIndex();

	if (VtxId > ToIndex.Num())
	{
		return Result;
	}

	const int32 Index = ToIndex[VtxId];
	

	if (Index != FDynamicMesh3::InvalidID && Index < InternalVertexCount)
	{
		bConstraintPositionsDirty = true;

		// Add should over-write any existing value for this key
		ConstraintPositionMap.Add(TTuple<int32, FConstraintPosition>(Index, FConstraintPosition(Pos, bPostFix)));

		Result = ConstraintWeightMap.Contains(VtxId);
	}
	return Result;
}

bool FConstrainedMeshOperator::UpdateConstraintWeight(const int32 VtxId, const double Weight)
{
	bool Result = false;
	const auto& ToIndex = VtxLinearization.ToIndex();

	if (VtxId > ToIndex.Num())
	{
		return Result;
	}

	const int32 Index = ToIndex[VtxId];
	if (Index != FDynamicMesh3::InvalidID && Index < InternalVertexCount)
	{
		bConstraintWeightsDirty = true;

		// Add should over-write any existing value for this key
		ConstraintWeightMap.Add(TTuple<int32, double>(Index, Weight));

		Result = ConstraintPositionMap.Contains(VtxId);
	}
	return Result;
}


bool FConstrainedMeshOperator::IsConstrained(const int32 VtxId) const
{
	bool Result = false;

	const auto& ToIndex = VtxLinearization.ToIndex();

	if (VtxId > ToIndex.Num())
	{
		return Result;
	}

	const int32 Index = ToIndex[VtxId];
	
	if (Index != FDynamicMesh3::InvalidID && Index < InternalVertexCount)
	{
		Result = ConstraintWeightMap.Contains(Index);
	}

	return Result;
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

	checkSlow(Num == InternalVertexCount);

	// 
	const auto& IndexToVtxId = VtxLinearization.ToId();
	const int32 MaxVtxId = IndexToVtxId.Num(); // NB: this is really max_used + 1 in the mesh.  See  FDynamicMesh3::MaxVertexID()



	LinearArray.Empty(MaxVtxId);
	LinearArray.AddUninitialized(MaxVtxId);
	
	// Copy over the boundary positions.
	{
		int32 BoundaryVertexCount = VertexCount - InternalVertexCount;
		for (int32 i = 0; i < BoundaryVertexCount; ++i)
		{
			const int32 VtxId = IndexToVtxId[i + InternalVertexCount];
			LinearArray[VtxId] = BoundaryPositions[i];
		}
	}

	// Update the internal positions.
	for (int32 i = 0; i < InternalVertexCount; ++i)
	{
		const int32 VtxId = IndexToVtxId[i];

		LinearArray[VtxId] = FVector3d(PositionalVector.XVector.coeff(i), PositionalVector.YVector.coeff(i), PositionalVector.ZVector.coeff(i));
	}

	return true;
}

void FConstrainedMeshOperator::ExtractInteriorVertexPositions(const FDynamicMesh3& DynamicMesh, FSOAPositions& VertexPositions) const
{
	VertexPositions.SetZero(InternalVertexCount);

	const auto& ToVtxId = VtxLinearization.ToId();

	for (int32 i = 0; i < InternalVertexCount; ++i)
	{
		const int32 VtxId = ToVtxId[i];
		const FVector3d& Pos = DynamicMesh.GetVertex(VtxId);
		VertexPositions.XVector.coeffRef(i) = Pos.X;
		VertexPositions.YVector.coeffRef(i) = Pos.Y;
		VertexPositions.ZVector.coeffRef(i) = Pos.Z;
	}

	
}

void FConstrainedMeshOperator::UpdateWithPostFixConstraints(FSOAPositions& PositionVector) const
{
	for (const auto& ConstraintPosition : ConstraintPositionMap)
	{
		const int32 Index = ConstraintPosition.Key;
		const FConstraintPosition& Constraint = ConstraintPosition.Value;

		checkSlow(Index < InternalVertexCount);

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
	, LaplacianVectors(FConstrainedMeshOperator::InternalVertexCount)
{
	

	// The current vertex positions 
	
	// Note: the OriginalInteriorPositions are being stored as member data 
	// for use if the solver is iterative.
	// FSOAPositions OriginalInteriorPositions; 
	ExtractInteriorVertexPositions(DynamicMesh, OriginalInteriorPositions);
	
	
	// The biharmonic part of the constrained solver
	//   Biharmonic := Laplacian^{T} * Laplacian
	
	const auto& Biharmonic = ConstrainedSolver->Biharmonic();

	// Compute the Laplacian Vectors
	//    := Biharmonic * VertexPostion
	// In the case of the cotangent laplacian this can be identified as the mean curvature * normal.
	checkSlow(LaplacianVectors.Num() == OriginalInteriorPositions.Num());

	for (int32 i = 0; i < 3; ++i)
	{
		LaplacianVectors.Array(i) = Biharmonic * OriginalInteriorPositions.Array(i);
	}
}

bool FConstrainedMeshDeformer::Deform(TArray<FVector3d>& PositionBuffer) 
{

	// Update constraints.  This only trigger solver rebuild if the weights were updated.
	UpdateSolverConstraints();

	// Allocate space for the result as a struct of arrays
	FSOAPositions SolutionVector(InternalVertexCount);

	// Solve the linear system
	// NB: the original positions will only be used if the underlying solver type is iterative	
	bool bSuccess = ConstrainedSolver->SolveWithGuess(OriginalInteriorPositions, LaplacianVectors, SolutionVector);
	
	// Move any vertices to match bPostFix constraints

	UpdateWithPostFixConstraints(SolutionVector);

	// Copy the result into the array of structs form.  
	// NB: this re-indexes so the results can be looked up using VtxId

	Linearize(SolutionVector, PositionBuffer);

	// the matrix solve state
	return bSuccess;

}


bool FBiHarmonicMeshSmoother::ComputeSmoothedMeshPositions(TArray<FVector3d>& UpdatedPositions)
{

	UpdateSolverConstraints();

	// Solves the constrained system and updates the mesh 

	FSOAPositions SolutionVector(InternalVertexCount);

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

	FSOAPositions SolutionVector(InternalVertexCount);

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
																						TArray<int32>* EdgeVtxs ) 
{
	bIsOperatorSymmetric = bIsSymmetricLaplacian(Scheme);


	// Construct the laplacian, and extract the mapping for vertices (VtxLinearization)
	TUniquePtr<FSparseMatrixD> Laplacian = ConstructLaplacian(Scheme, DynamicMesh, VtxLinearization, EdgeVtxs);
	Laplacian->makeCompressed();
	return Laplacian;
};

TUniquePtr<FSparseMatrixD> FBiHarmonicDiffusionMeshSmoother::ConstructDiffusionOperator( const ELaplacianWeightScheme Scheme,
																						 const FDynamicMesh3& DynamicMesh,
																						 bool& bIsOperatorSymmetric,
																						 FVertexLinearization& Linearization,
																						 TArray<int32>* EdgeVtxs ) 
{
	bIsOperatorSymmetric = true;
	
	// Construct the laplacian, and extract the mapping for vertices (VtxLinearization)
	TUniquePtr<FSparseMatrixD> Laplacian = ConstructLaplacian(Scheme, DynamicMesh, VtxLinearization, EdgeVtxs);
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