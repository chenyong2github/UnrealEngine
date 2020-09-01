// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshUVSolver.h"

#include "Solvers/MatrixInterfaces.h"
#include "Solvers/LaplacianMatrixAssembly.h"
#include "MatrixSolver.h"
#include "MeshBoundaryLoops.h"
#include "MeshQueries.h"


//
// Extension of TSparseMatrixAssembler suitable for eigen sparse matrix,
// that stores each element twice. The DNCP linear system is 2Vx2V for a mesh with
// V vertices, ie a block matrix [ [X;0] , [0;Y] ], where X and Y are copies of the
// Cotan-Laplacian matrix. So in the AddEntryFunc below when we get a new entry we
// store it in both locations.
//
class FEigenDNCPSparseMatrixAssembler : public UE::Solvers::TSparseMatrixAssembler<double>
{
public:
	typedef FSparseMatrixD::Scalar    ScalarT;
	typedef Eigen::Triplet<ScalarT>  MatrixTripletT;

	TUniquePtr<FSparseMatrixD> Matrix;
	std::vector<MatrixTripletT> EntryTriplets;

	int32 N;

	FEigenDNCPSparseMatrixAssembler(int32 RowsI, int32 ColsJ)
	{
		check(RowsI == ColsJ);
		N = RowsI;

		Matrix = MakeUnique<FSparseMatrixD>(2 * N, 2 * N);

		ReserveEntriesFunc = [this](int32 NumElements)
		{
			EntryTriplets.reserve(NumElements);
		};

		AddEntryFunc = [this](int32 i, int32 j, double Value)
		{
			// set upper-left block value
			EntryTriplets.push_back(MatrixTripletT(i, j, Value));
			// set lower-right block value
			EntryTriplets.push_back(MatrixTripletT(N + i, N + j, Value));
		};
	}

	void ExtractResult(FSparseMatrixD& Result)
	{
		Matrix->setFromTriplets(EntryTriplets.begin(), EntryTriplets.end());
		Matrix->makeCompressed();
		Result.swap(*Matrix);
	}
};




//
// Build DNCP matrix
//
//
static void ConstructNaturalConformalLaplacianSystem(
	const FDynamicMesh3& DynamicMesh,
	const FVertexLinearization& VertexMap,
	const TArray<int32>& PinnedVertices,
	FSparseMatrixD& Laplacian)
{
	typedef FSparseMatrixD::Scalar  ScalarT;
	typedef Eigen::Triplet<ScalarT> MatrixTripletT;

	const TArray<int32>& ToMeshV = VertexMap.ToId();
	const TArray<int32>& ToIndex = VertexMap.ToIndex();
	const int32 NumVerts = VertexMap.NumVerts();

	// Construct 2Nx2N system that includes both X and Y values for UVs. We will use block form [X, 0; 0, Y]
	FEigenDNCPSparseMatrixAssembler DNCPLaplacianAssembler(NumVerts, NumVerts);
	UE::MeshDeformation::ConstructFullCotangentLaplacian<double>(DynamicMesh, VertexMap, DNCPLaplacianAssembler, 
		UE::MeshDeformation::ECotangentWeightMode::ClampedMagnitude,
		UE::MeshDeformation::ECotangentAreaMode::NoArea );
	FSparseMatrixD CotangentMatrix;
	DNCPLaplacianAssembler.ExtractResult(CotangentMatrix);
	// we want diagonal to be positive, so that sign of quadratic form is the same as
	// the area matrix (which is a positive area)
	CotangentMatrix = CotangentMatrix * -1.0;

	// construct Area matrix. This matrix calculates the 2D area of the mesh.
	int32 N = NumVerts;
	FSparseMatrixD AreaMatrix(2 * N, 2 * N);
	FMeshBoundaryLoops Loops(&DynamicMesh, true);
	for (FEdgeLoop& Loop : Loops.Loops)
	{
		Algo::Reverse(Loop.Vertices);		// reverse loop to handle UE mesh orientation
		int32 NumV = Loop.GetVertexCount();
		for (int32 k = 0; k < NumV; ++k)
		{
			int32 a = Loop.Vertices[k];
			int32 jx = ToIndex[a];
			int32 jy = jx + N;
			int32 b = Loop.Vertices[(k + 1) % NumV];
			int32 kx = ToIndex[b];
			int32 ky = kx + N;

			AreaMatrix.coeffRef(jx, ky) = AreaMatrix.coeffRef(jx, ky) + 1;
			AreaMatrix.coeffRef(ky, jx) = AreaMatrix.coeffRef(ky, jx) + 1;
			AreaMatrix.coeffRef(kx, jy) = AreaMatrix.coeffRef(kx, jy) - 1;
			AreaMatrix.coeffRef(jy, kx) = AreaMatrix.coeffRef(jy, kx) - 1;
		}
	}


	// test code that computes the 2D area of the input mesh, as well as the quadratic forms for the Cotan and
	// Area matrices. On a planar mesh these should all produce the same value
	//Eigen::Matrix<double, Eigen::Dynamic, 1> UVVector(2 * N);
	//for (int32 vid : DynamicMesh.VertexIndicesItr())
	//{
	//	int32 Index = VertexMap.GetIndex(vid);
	//	FVector3d Pos = DynamicMesh.GetVertex(vid);
	//	UVVector.coeffRef(Index) = Pos.X;
	//	UVVector.coeffRef(Index + N) = Pos.Y;
	//}
	//FVector2d VolArea = TMeshQueries<FDynamicMesh3>::GetVolumeArea(DynamicMesh);
	//Eigen::Matrix<double, Eigen::Dynamic, 1> AreaMeasure = 0.5 * UVVector.transpose() * AreaMatrix * UVVector;
	//Eigen::Matrix<double, Eigen::Dynamic, 1> CotanMeasure = 0.5 * UVVector.transpose() * CotangentMatrix * UVVector;

	// assemble Conformal energy matrix
	FSparseMatrixD Lc = CotangentMatrix - AreaMatrix;

	// test code that computes the conformal energy quadratic form. This should be zero on a planar mesh.
	//Eigen::Matrix<double, Eigen::Dynamic, 1> ConformalMeasure = 0.5 * UVVector.transpose() * Lc * UVVector;

	// set fixed vertex rows to M(i,i) = 1;
	FSparseMatrixD LcCons = Lc;
	for (int32 PinnedVertID : PinnedVertices)
	{
		int32 k = ToIndex[PinnedVertID];
		// clear existing rows
		LcCons.row(k) *= 0;
		LcCons.row(k + N) *= 0;
		// set to identity rows
		LcCons.coeffRef(k, k) = 1.0;
		LcCons.coeffRef(k + N, k + N) = 1.0;
	}

	// TODO: we can move the constrained columns to the RHS to keep the matrix symmetric.
	// This would allow for more efficient solving, if we had a symmetric solver...however it
	// also means we need a way to pass back the vector that needs to be added to the RHS

	// make the compressed result
	Laplacian = MoveTemp(LcCons);
	Laplacian.makeCompressed();
}






//
// Solve a the natural/free-boundary conformal parameterization problem defined by the given system matrix
// (likely generated by ConstructNaturalConformalLaplacianSystem() above) using the specified linear solver.
// 
// Requires that FixedIndices/Positions pairs define at least two constraint points for the solution to be well-defined.
// Assumes that these rows are also constrained in the System Matrix
static bool SolveDiscreteNaturalConformalSystem(
	const FSparseMatrixD& CombinedUVSystemMatrix, const EMatrixSolverType MatrixSolverType,
	const TArray<int32>& FixedIndices, const TArray<FVector2d>& FixedPositions, TArray<FVector2d>& Solution)
{
	// create a suitable matrix solver
	TUniquePtr<IMatrixSolverBase> MatrixSolver = ContructMatrixSolver(MatrixSolverType);
	MatrixSolver->SetUp(CombinedUVSystemMatrix, false);

	// set the constraint positions in the RHS
	int32 N = CombinedUVSystemMatrix.rows() / 2;
	Eigen::Matrix<double, Eigen::Dynamic, 1> RHSVector(2 * N);
	RHSVector.setZero();
	for (int32 k = 0; k < FixedIndices.Num(); ++k)
	{
		int32 Index = FixedIndices[k];
		RHSVector.coeffRef(Index) = FixedPositions[k].X;
		RHSVector.coeffRef(Index + N) = FixedPositions[k].Y;
	}

	// solve the linear system
	Eigen::Matrix<double, Eigen::Dynamic, 1> SolutionVector(2 * N);
	MatrixSolver->Solve(RHSVector, SolutionVector);
	bool bSuccess = MatrixSolver->bSucceeded();

	// extract the solution if matrix solve succeeded, or set result to zero
	Solution.SetNum(N);
	if (bSuccess)
	{
		for (int32 k = 0; k < N; ++k)
		{
			Solution[k] = FVector2d(SolutionVector.coeffRef(k), SolutionVector.coeffRef(k + N));
		}
	}
	else
	{
		for (int32 k = 0; k < N; ++k)
		{
			Solution[k] = FVector2d::Zero();
		}
	}

	return bSuccess;
}










FConstrainedMeshUVSolver::~FConstrainedMeshUVSolver()
{
}

FConstrainedMeshUVSolver::FConstrainedMeshUVSolver(const FDynamicMesh3& DynamicMesh, EUVSolveType UVSolveTypeIn)
{
	UVSolveType = UVSolveTypeIn;

	// compute linearization so we can store constraints at linearized indices
	VtxLinearization.Reset(DynamicMesh);
}





bool FConstrainedMeshUVSolver::SolveUVs(const FDynamicMesh3* DynamicMesh, TArray<FVector2d>& UVBuffer)
{
	check(UVSolveType == EUVSolveType::NaturalConformal);

	// create list of pinned vertices and positions
	TArray<int32> FixedVertexIDs;
	TArray<int32> FixedIndices;
	TArray<FVector2d> FixedUVs;
	for (auto& ElemPair : ConstraintMap)
	{
		FixedIndices.Add(ElemPair.Value.ConstraintIndex);
		FixedVertexIDs.Add(ElemPair.Value.ElementID);
		FixedUVs.Add(ElemPair.Value.Position);
	}

	// build DNCP system
	TUniquePtr<FSparseMatrixD> UVSystemMatrix = MakeUnique<FSparseMatrixD>();
	ConstructNaturalConformalLaplacianSystem(*DynamicMesh, VtxLinearization, FixedVertexIDs, *UVSystemMatrix);

	// transfer to solver and solve
	TArray<FVector2d> Solution;
	bool bSolveOK = SolveDiscreteNaturalConformalSystem(*UVSystemMatrix, EMatrixSolverType::LU,
		FixedIndices, FixedUVs, Solution);

	ensure(bSolveOK);
	if (bSolveOK == false)
	{
		// If solve failed we will try QR solver which is more robust.
		// This should perhaps be optional as the QR solve is much more expensive...
		Solution.Reset();
		bSolveOK = SolveDiscreteNaturalConformalSystem(*UVSystemMatrix, EMatrixSolverType::QR,
			FixedIndices, FixedUVs, Solution);
		ensure(bSolveOK);
	}

	// copy back to input buffer
	UVBuffer.SetNum(DynamicMesh->MaxVertexID());
	for (int32 Index = 0; Index < VtxLinearization.NumIndices(); ++Index)
	{
		int32 Id = VtxLinearization.GetId(Index);
		UVBuffer[Id] = Solution[Index];
	}

	return true;
}



void FConstrainedMeshUVSolver::AddConstraint(const int32 VtxId, const double Weight, const FVector2d& Pos, const bool bPostFix)
{
	if ( ensure(VtxLinearization.IsValidId(VtxId)) == false) return;
	int32 Index = VtxLinearization.GetIndex(VtxId);

	FUVConstraint NewConstraint;
	NewConstraint.ElementID = VtxId;
	NewConstraint.ConstraintIndex = Index;
	NewConstraint.Position = Pos;
	NewConstraint.Weight = Weight;
	NewConstraint.bPostFix = bPostFix;

	ConstraintMap.Add(Index, NewConstraint);

	bConstraintPositionsDirty = true;
	bConstraintWeightsDirty = true;
}


bool FConstrainedMeshUVSolver::UpdateConstraintPosition(const int32 VtxId, const FVector2d& NewPosition, const bool bPostFix)
{
	if (ensure(VtxLinearization.IsValidId(VtxId)) == false) return false;
	int32 Index = VtxLinearization.GetIndex(VtxId);

	FUVConstraint* Found = ConstraintMap.Find(Index);
	if (ensure(Found != nullptr))
	{
		Found->Position = NewPosition;
		Found->bPostFix = bPostFix;
		bConstraintPositionsDirty = true;
		return true;
	}
	return false;
}


bool FConstrainedMeshUVSolver::UpdateConstraintWeight(const int32 VtxId, const double NewWeight)
{
	if (ensure(VtxLinearization.IsValidId(VtxId)) == false) return false;
	int32 Index = VtxLinearization.GetIndex(VtxId);

	FUVConstraint* Found = ConstraintMap.Find(Index);
	if ( ensure(Found != nullptr) )
	{
		Found->Weight = NewWeight;
		bConstraintWeightsDirty = true;
		return true;
	}
	return false;
}


bool FConstrainedMeshUVSolver::IsConstrained(const int32 VtxId) const
{
	if (VtxLinearization.IsValidId(VtxId) == false) return false;
	int32 Index = VtxLinearization.GetIndex(VtxId);
	return ConstraintMap.Contains(Index);
}


void FConstrainedMeshUVSolver::ClearConstraints()
{ 
	ConstraintMap.Empty();
	bConstraintPositionsDirty = true;
	bConstraintWeightsDirty = true;
}



