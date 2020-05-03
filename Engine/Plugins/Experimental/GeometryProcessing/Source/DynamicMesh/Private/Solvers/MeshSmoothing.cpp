// Copyright Epic Games, Inc. All Rights Reserved.

#include "Solvers/MeshSmoothing.h"
#include "Solvers/Internal/MeshDiffusionSmoothing.h"
#include "Solvers/Internal/ConstrainedMeshSmoothers.h"


void UE::MeshDeformation::ComputeSmoothing_BiHarmonic(const ELaplacianWeightScheme WeightScheme, const FDynamicMesh3& OriginalMesh,
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
	const EMatrixSolverType MatrixSolverType = (bIsSymmetricLaplacian(WeightScheme)) ? EMatrixSolverType::PCG : EMatrixSolverType::LU;

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

void UE::MeshDeformation::ComputeSmoothing_ImplicitBiHarmonicPCG(const ELaplacianWeightScheme WeightScheme, const FDynamicMesh3& OriginalMesh,
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

void UE::MeshDeformation::ComputeSmoothing_Diffusion(const ELaplacianWeightScheme WeightScheme, const FDynamicMesh3& OriginalMesh, bool bForwardEuler,
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

