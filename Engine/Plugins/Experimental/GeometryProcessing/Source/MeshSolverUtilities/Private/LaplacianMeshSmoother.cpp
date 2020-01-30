// Copyright Epic Games, Inc. All Rights Reserved.

#include "LaplacianMeshSmoother.h"

#include "FSOAPositions.h"

#include "LaplacianOperators.h"
#include "MatrixSolver.h"
#include "MeshSmoothingUtilities.h"

#ifdef TIME_LAPLACIAN_SMOOTHERS

#include "ProfilingDebugging/ScopedTimers.h"
DEFINE_LOG_CATEGORY_STATIC(LogMeshSmoother, Log, All);

#endif

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
	{
		const auto ToVertId = VtxLinearization.ToId();
		BoundaryPositions.SetZero(BoundaryVertexCount);
		for (int32 i = 0; i < BoundaryVertexCount; ++i)
		{
			const int32 VtxId = ToVertId[i + InternalVertexCount];
			const FVector3d Pos = DynamicMesh.GetVertex(VtxId);

			BoundaryPositions.XVector[i] = Pos.X;
			BoundaryPositions.YVector[i] = Pos.Y;
			BoundaryPositions.ZVector[i] = Pos.Z;
		}
	}

	checkSlow(LaplacianInternal.rows() == LaplacianInternal.cols());

	TUniquePtr<FSparseMatrixD> LTLPtr( new FSparseMatrixD(LaplacianInternal.rows(), LaplacianInternal.cols()) );
	FSparseMatrixD& LTLMatrix = *(LTLPtr);

	bool bIsLaplacianSymmetric = (Scheme == ELaplacianWeightScheme::Valence || Scheme == ELaplacianWeightScheme::Uniform);

	if (bIsLaplacianSymmetric)
	{
		// Laplacian is symmetric, i.e. equal to its transpose
		LTLMatrix        = LaplacianInternal * LaplacianInternal;
		BoundaryOperator = -1. * LaplacianInternal * LaplacianBoundary;
		
		ConstrainedSolver.Reset(new FConstrainedSolver(LTLPtr, MatrixSolverType));
	}
	else
	{
		// the laplacian 
		LTLMatrix        = LaplacianInternal.transpose() * LaplacianInternal;
		BoundaryOperator = -1. * LaplacianInternal.transpose() * LaplacianBoundary;
		
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

bool FConstrainedMeshOperator::CopyInternalPositions(const FSOAPositions& PositionalVector, TArray<FVector3d>& LinearArray) const
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
	const auto& ToVtxId = VtxLinearization.ToId();
	const int32 MaxVtxId = ToVtxId.Num(); // NB: this is really max_used + 1 in the mesh.  See  FDynamicMesh3::MaxVertexID()

	if (LinearArray.Num() != MaxVtxId)
	{
		return false;
	}

	// Update the internal positions.
	for (int32 i = 0; i < InternalVertexCount; ++i)
	{
		const int32 VtxId = ToVtxId[i];

		LinearArray[VtxId] = FVector3d(PositionalVector.XVector.coeff(i), PositionalVector.YVector.coeff(i), PositionalVector.ZVector.coeff(i));
	}

	return true;
}

bool FConstrainedMeshOperator::CopyBoundaryPositions(TArray<FVector3d>& LinearArray) const
{
	const auto& ToVtxId = VtxLinearization.ToId();
	const int32 MaxVtxId = ToVtxId.Num();

	if (LinearArray.Num() != MaxVtxId)
	{
		return false;
	}

	int32 BoundaryVertexCount = VertexCount - InternalVertexCount;

	for (int32 i = 0; i < BoundaryVertexCount; ++i)
	{
		const int32 VtxId = ToVtxId[i + InternalVertexCount];
		LinearArray[VtxId] = FVector3d(BoundaryPositions.XVector.coeff(i), BoundaryPositions.YVector.coeff(i), BoundaryPositions.ZVector.coeff(i)); //BoundaryPositions[i];
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

	// Allocate Position Buffer for random access writes
	int32 MaxVtxId = VtxLinearization.ToId().Num();
	PositionBuffer.Empty(MaxVtxId);
	PositionBuffer.AddUninitialized(MaxVtxId);

	// Export the computed internal positions:
	// Copy the results into the array of structs form.  
	// NB: this re-indexes so the results can be looked up using VtxId

	CopyInternalPositions(SolutionVector, PositionBuffer);

	// Copy the boundary
	// NB: this re-indexes so the results can be looked up using VtxId
	CopyBoundaryPositions(PositionBuffer);

	// the matrix solve state
	return bSuccess;

}


bool FBiHarmonicMeshSmoother::ComputeSmoothedMeshPositions(TArray<FVector3d>& UpdatedPositions)
{

	UpdateSolverConstraints();

	// Compute the source vector
	FSOAPositions SourceVector(InternalVertexCount);

	if (InternalVertexCount != VertexCount) // have boundary points
	{
		for (int32 dir = 0; dir < 3; ++dir)
		{
			SourceVector.Array(dir) = BoundaryOperator * BoundaryPositions.Array(dir);
		}
	}
	else
	{
		SourceVector.SetZero(InternalVertexCount);
	}

	// Solves the constrained system and updates the mesh 

	FSOAPositions SolutionVector(InternalVertexCount);

	bool bSuccess = ConstrainedSolver->Solve(SourceVector, SolutionVector);


	// Move any vertices to match bPostFix constraints

	UpdateWithPostFixConstraints(SolutionVector);

	// Allocate Position Buffer for random access writes
	int32 MaxVtxId = VtxLinearization.ToId().Num();
	UpdatedPositions.Empty(MaxVtxId);
	UpdatedPositions.AddUninitialized(MaxVtxId);

	// Export the computed internal positions:
	// Copy the results into the array of structs form.  
	// NB: this re-indexes so the results can be looked up using VtxId

	CopyInternalPositions(SolutionVector, UpdatedPositions);

	// Copy the boundary
	// NB: this re-indexes so the results can be looked up using VtxId
	CopyBoundaryPositions(UpdatedPositions);

	return bSuccess;
}

bool FCGBiHarmonicMeshSmoother::ComputeSmoothedMeshPositions(TArray<FVector3d>& UpdatedPositions)
{

	UpdateSolverConstraints();

	// Compute the source vector
	FSOAPositions SourceVector(InternalVertexCount);

	if (InternalVertexCount != VertexCount) // have boundary points
	{
		for (int32 dir = 0; dir < 3; ++dir)
		{
			SourceVector.Array(dir) = BoundaryOperator * BoundaryPositions.Array(dir);
		}
	}
	else
	{
		SourceVector.SetZero(InternalVertexCount);
	}

	// Solves the constrained system and updates the mesh 

	// Solves the constrained system

	FSOAPositions SolutionVector(InternalVertexCount);

	bool bSuccess = ConstrainedSolver->Solve(SourceVector, SolutionVector);


	// Move any vertices to match bPostFix constraints

	UpdateWithPostFixConstraints(SolutionVector);

	// Allocate Position Buffer for random access writes
	int32 MaxVtxId = VtxLinearization.ToId().Num();
	UpdatedPositions.Empty(MaxVtxId);
	UpdatedPositions.AddUninitialized(MaxVtxId);

	// Export the computed internal postions:
	// Copy the results into the array of structs form.  
	// NB: this re-indexes so the results can be looked up using VtxId

	CopyInternalPositions(SolutionVector, UpdatedPositions);

	// Copy the boundary
	// NB: this re-indexes so the results can be looked up using VtxId
	CopyBoundaryPositions(UpdatedPositions);

	return bSuccess;
}



FDiffusionIntegrator::FDiffusionIntegrator(const FDynamicMesh3& DynamicMesh, const ELaplacianWeightScheme Scheme)
{
	Id = 0;
	bIsSymmetric = false;
	MinDiagonalValue = 0;

	VertexCount = DynamicMesh.VertexCount();
	
}

void FDiffusionIntegrator::Initialize(const FDynamicMesh3& DynamicMesh, const ELaplacianWeightScheme Scheme)
{
	// Construct the laplacian, and extract the mapping for vertices (VtxLinearization)
	//DiffusionOperator = ConstructDiffusionOperator(Scheme, DynamicMesh, bIsSymmetric, VtxLinearization, &EdgeVerts);

	
	ConstructOperators(Scheme, DynamicMesh, bIsSymmetric, VtxLinearization, DiffusionOperator, BoundaryOperator);

	const int32 BoundaryVertexCount = VtxLinearization.NumBoundaryVerts();
	InternalVertexCount = VertexCount - BoundaryVertexCount;

	// Allocate the double buffers.
	Tmp[0].SetZero(InternalVertexCount);
	Tmp[1].SetZero(InternalVertexCount);

	const auto& ToVertId = VtxLinearization.ToId();

	// Extract current internal positions.
	for (int32 i = 0; i < InternalVertexCount; ++i)
	{
		const int32 VtxId = ToVertId[i];

		const FVector3d Pos = DynamicMesh.GetVertex(VtxId);
		Tmp[0].XVector[i] = Pos.X;
		Tmp[0].YVector[i] = Pos.Y;
		Tmp[0].ZVector[i] = Pos.Z;
	}

	// backup the locations of the boundary verts.
	{
		BoundaryPositions.SetZero(BoundaryVertexCount);
		for (int32 i = 0; i < BoundaryVertexCount; ++i)
		{
			const int32 VtxId = ToVertId[i + InternalVertexCount];
			const FVector3d Pos = DynamicMesh.GetVertex(VtxId);

			BoundaryPositions.XVector[i] = Pos.X;
			BoundaryPositions.YVector[i] = Pos.Y;
			BoundaryPositions.ZVector[i] = Pos.Z;
		}
	}

	const FSparseMatrixD&  M = DiffusionOperator;

	// Find the min diagonal entry (all should be negative).
	int32 Rank = M.rows();
	MinDiagonalValue = FSparseMatrixD::Scalar(0);
	for (int32 i = 0; i < Rank; ++i)
	{
		auto Diag = M.coeff(i, i);
		MinDiagonalValue = FMath::Min(Diag, MinDiagonalValue);
	}
	// The matrix should have a row for each internal vertex
	checkSlow(Rank == InternalVertexCount);

#if 0
	// testing - how to print the matrix to debug output 

	std::stringstream  ss;
	ss << Eigen::MatrixXd(M) << std::endl;

	FString Foo = ss.str().c_str();
	FPlatformMisc::LowLevelOutputDebugStringf(*Foo);
#endif

}



void FDiffusionIntegrator::Integrate_ForwardEuler(const int32 NumSteps, const double Speed)
{
	double Alpha = FMath::Clamp(Speed, 0., 1.);

	FSparseMatrixD::Scalar TimeStep = -Alpha / MinDiagonalValue;
	Id = 0;
	for (int32 s = 0; s < NumSteps; ++s)
	{

		int32 SrcBuffer = Id;
		Id = 1 - Id;
		Tmp[Id].XVector = Tmp[SrcBuffer].XVector + TimeStep * ( DiffusionOperator * Tmp[SrcBuffer].XVector + BoundaryOperator * BoundaryPositions.XVector );
		Tmp[Id].YVector = Tmp[SrcBuffer].YVector + TimeStep * ( DiffusionOperator * Tmp[SrcBuffer].YVector + BoundaryOperator * BoundaryPositions.YVector );
		Tmp[Id].ZVector = Tmp[SrcBuffer].ZVector + TimeStep * ( DiffusionOperator * Tmp[SrcBuffer].ZVector + BoundaryOperator * BoundaryPositions.ZVector ); 

	}

}



void FDiffusionIntegrator::Integrate_BackwardEuler(const EMatrixSolverType MatrixSolverType, const int32 NumSteps, const double TimeStepSize)
{

	//typedef typename TMatrixSolverTrait<EMatrixSolverType::LU>::MatrixSolverType   MatrixSolverType;

	// We solve 
	// p^{n+1} - dt * L[p^{n+1}] = p^{n} + dt * B[boundaryPts]
	// 
	// i.e.
	// [I - dt * L ] p^{n+1} = p^{n} + dt * B[boundaryPts]
	//
	// NB: in the case of the cotangent laplacian this would be better if we broke the L int
	// L = (A^{-1}) H  where A is the "area matrix" (think "mass matrix"), then this would
	// become
	// [A - dt * H] p^{n+1} = Ap^{n}  dt * A *B[boundaryPts]
	//  
	// A - dt * H would be symmetric
	//


	
	// Identity matrix
	FSparseMatrixD Ident(DiffusionOperator.rows(), DiffusionOperator.cols());
	Ident.setIdentity();

	FSparseMatrixD::Scalar TimeStep = FMath::Abs(TimeStepSize);// Alpha * FMath::Min(Intensity, 1.e6);
	
	FSparseMatrixD SparseMatrix = Ident -TimeStep * DiffusionOperator;

	
	SparseMatrix.makeCompressed();

	TUniquePtr<IMatrixSolverBase> MatrixSolver = ContructMatrixSolver(MatrixSolverType);

	MatrixSolver->SetUp(SparseMatrix, bIsSymmetric);

	// We are going to solve the system 
	FSOAPositions Source(InternalVertexCount);

	if (MatrixSolver->bIsIterative())
	{
		IIterativeMatrixSolverBase* IterativeSolver = (IIterativeMatrixSolverBase*)MatrixSolver.Get();

		bool bForceSingleThreaded = false;
		Id = 0;
		for (int32 s = 0; s < NumSteps; ++s)
		{

			int32 SrcBuffer = Id;
			Id = 1 - Id;

			for (int32 i = 0; i < 3; ++i)
			{
				Source.Array(i) = Tmp[SrcBuffer].Array(i) + TimeStep *  BoundaryOperator * BoundaryPositions.Array(i);
			}
			// Old solution is the guess.
			IterativeSolver->SolveWithGuess(Tmp[SrcBuffer], Source, Tmp[Id]);

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

			for (int32 i = 0; i < 3; ++i)
			{
				Source.Array(i) = Tmp[SrcBuffer].Array(i) + TimeStep * BoundaryOperator * BoundaryPositions.Array(i);
			}

			MatrixSolver->Solve(Source, Tmp[Id]);
		}
	}
	

}

void FDiffusionIntegrator::GetPositions(TArray<FVector3d>& PositionBuffer) const
{
	// Allocate Position Buffer for random access writes
	int32 MaxVtxId = VtxLinearization.ToId().Num();
	PositionBuffer.Empty(MaxVtxId);
	PositionBuffer.AddUninitialized(MaxVtxId);

	CopyInternalPositions(Tmp[Id], PositionBuffer);


	// Copy the boundary
	// NB: this re-indexes so the results can be looked up using VtxId
	CopyBoundaryPositions(PositionBuffer);

}

bool FDiffusionIntegrator::CopyInternalPositions(const FSOAPositions& PositionalVector, TArray<FVector3d>& LinearArray) const
{
	// Number of positions

	const int32 Num = PositionalVector.XVector.rows();

	// early out if the x,y,z arrays in the PositionalVector have different lengths
	if (!PositionalVector.bHasSize(Num))
	{
		return false;
	}

	// 
	const auto& ToVtxId  = VtxLinearization.ToId();
	const int32 MaxVtxId = ToVtxId.Num(); // NB: this is really max_used + 1 in the mesh.  See  FDynamicMesh3::MaxVertexID()
	const int32 BoundaryVertexCount = VtxLinearization.NumBoundaryVerts();

	if (MaxVtxId != LinearArray.Num())
	{
		return false;
	}

	// Copy the updated internal vertex locations over
	for (int32 i = 0; i < InternalVertexCount; ++i)
	{
		const int32 VtxId = ToVtxId[i];

		LinearArray[VtxId] = FVector3d(PositionalVector.XVector.coeff(i), PositionalVector.YVector.coeff(i), PositionalVector.ZVector.coeff(i));
	}

	return true;
}
                       
bool FDiffusionIntegrator::CopyBoundaryPositions(TArray<FVector3d>& LinearArray) const
{
	const auto& ToVtxId = VtxLinearization.ToId();
	const int32 MaxVtxId = ToVtxId.Num();

	if (LinearArray.Num() != MaxVtxId)
	{
		return false;
	}

	int32 BoundaryVertexCount = VertexCount - InternalVertexCount;

	for (int32 i = 0; i < BoundaryVertexCount; ++i)
	{
		const int32 VtxId = ToVtxId[i + InternalVertexCount];
		LinearArray[VtxId] = FVector3d(BoundaryPositions.XVector.coeff(i), BoundaryPositions.YVector.coeff(i), BoundaryPositions.ZVector.coeff(i));
	}

	return true;
}



void FLaplacianDiffusionMeshSmoother::ConstructOperators( const ELaplacianWeightScheme Scheme,
	                                                      const FDynamicMesh3& Mesh,
	                                                      bool& bIsOperatorSymmetric,
	                                                      FVertexLinearization& Linearization,
	                                                      FSparseMatrixD& DiffusionOp,
	                                                      FSparseMatrixD& BoundaryOp) 
{
	bIsOperatorSymmetric = bIsSymmetricLaplacian(Scheme);
	ConstructLaplacian(Scheme, Mesh, VtxLinearization, DiffusionOp, BoundaryOp);
}


void FBiHarmonicDiffusionMeshSmoother::ConstructOperators( const ELaplacianWeightScheme Scheme,
														   const FDynamicMesh3& Mesh,
														   bool& bIsOperatorSymmetric,
														   FVertexLinearization& Linearization,
														   FSparseMatrixD& DiffusionOp,
	                                                       FSparseMatrixD& BoundaryOp) 
{
	bIsOperatorSymmetric = true;
	
	FSparseMatrixD Laplacian; 
	FSparseMatrixD BoundaryTerms;
	ConstructLaplacian(Scheme, Mesh, VtxLinearization, Laplacian, BoundaryTerms);

	bool bIsLaplacianSymmetric = bIsSymmetricLaplacian(Scheme);

	// It is actually unclear the best way to approximate the boundary conditions in this case.  
	// because we are repeatedly applying the operator ( for example thing about the way ( f(x+d)-f(x-d) )/ d will spread if you apply it twice
	// as opposed to (f(x+d)-2f(x) + f(x-d) ) / d*d

	// Anyhow here is a guess..

	if (bIsLaplacianSymmetric)
	{
		DiffusionOp = -1. * Laplacian * Laplacian;
		BoundaryOp  = -1. * Laplacian * BoundaryTerms;
	}
	else
	{
		FSparseMatrixD LTran = Laplacian.transpose();
		DiffusionOp = -1. * LTran * Laplacian;
		BoundaryOp  = -1. * LTran * BoundaryTerms;
	}
	
	DiffusionOp.makeCompressed();
	BoundaryOp.makeCompressed();

}


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


#ifdef TIME_LAPLACIAN_SMOOTHERS
	FString DebugLogString = FString::Printf(TEXT("Biharmonic Smoothing of mesh with %d verts "), OriginalMesh.VertexCount()) + LaplacianSchemeName(WeightScheme) + MatrixSolverName(MatrixSolverType);

	FScopedDurationTimeLogger Timer(DebugLogString);
#endif

	const double TimeStep = Speed * FMath::Min(Intensity, 1.e6);

	FBiHarmonicDiffusionMeshSmoother BiHarmonicDiffusionSmoother(OriginalMesh, WeightScheme);

	BiHarmonicDiffusionSmoother.Integrate_BackwardEuler(MatrixSolverType, NumIterations, TimeStep);

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
#ifdef TIME_LAPLACIAN_SMOOTHERS
	FString DebugLogString = FString::Printf(TEXT("PCG Biharmonic Smoothing of mesh with %d verts "), OriginalMesh.VertexCount()) + LaplacianSchemeName(WeightScheme);

	FScopedDurationTimeLogger Timer(DebugLogString);
#endif 
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

#ifdef TIME_LAPLACIAN_SMOOTHERS
	FString DebugLogString = FString::Printf(TEXT("Diffusion Smoothing of mesh with %d verts"), OriginalMesh.VertexCount());
	if (!bForwardEuler)
	{
		DebugLogString += MatrixSolverName(MatrixSolverType);
	}

	FScopedDurationTimeLogger Timer(DebugLogString);
#endif
	if (IterationCount < 1) return;

	FLaplacianDiffusionMeshSmoother Smoother(OriginalMesh, WeightScheme);

	if (bForwardEuler)
	{
		Smoother.Integrate_ForwardEuler(IterationCount, Speed);
	}
	else
	{
		const double TimeStep = Speed * FMath::Min(Intensity, 1.e6);
		Smoother.Integrate_BackwardEuler(MatrixSolverType, IterationCount, TimeStep);
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