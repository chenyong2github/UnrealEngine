// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmoothingOps/IterativeSmoothingOp.h"
#include "Solvers/MeshSmoothing.h"
#include "MeshWeights.h"
#include "MeshCurvature.h"
#include "Async/ParallelFor.h"

FIterativeSmoothingOp::FIterativeSmoothingOp(const FDynamicMesh3* Mesh, const FSmoothingOpBase::FOptions& OptionsIn) :
	FSmoothingOpBase(Mesh, OptionsIn)
{
	SmoothedBuffer = PositionBuffer;
}

void FIterativeSmoothingOp::CalculateResult(FProgressCancel* Progress)
{
	// Update the values in the position buffer with smoothed positions.
	if (SmoothOptions.bUseImplicit)
	{
		if (SmoothOptions.bUniform)
		{
			Smooth_MeanValue();
		}
		else
		{
			Smooth_Implicit_Cotan();
		}
	}
	else
	{
		Smooth_Forward(SmoothOptions.bUniform);
	}

	// Copy the results back into the result mesh and update normals
	UpdateResultMesh();

}

void FIterativeSmoothingOp::Smooth_Implicit_Cotan()
{
	double Intensity = 1.;
	UE::MeshDeformation::ComputeSmoothing_BiHarmonic(ELaplacianWeightScheme::ClampedCotangent, 
		*ResultMesh, SmoothOptions.SmoothAlpha, Intensity, SmoothOptions.Iterations, PositionBuffer);
}


void FIterativeSmoothingOp::Smooth_MeanValue()
{
	double Intensity = 1.;
	UE::MeshDeformation::ComputeSmoothing_BiHarmonic(ELaplacianWeightScheme::MeanValue,
		*ResultMesh, SmoothOptions.SmoothAlpha, Intensity, SmoothOptions.Iterations, PositionBuffer);
}


// Each iteration is equivalent to time advancing the diffusion system 
// 
//  d p / dt = k*k L[ p ]
// 
// with forward Euler
//  
// p^{n+1} = p^{n} + dt k * k L[ p^{n} ]
// 
// Where { L[p] }_i = Sum[ w_{ij} p_j ]  
//        
//        and w_{ii} = - Sum[ w_{ij}, j != i }
//
// Here SmoothSpeed = -dt * k * k * w_{ii}
void FIterativeSmoothingOp::Smooth_Forward(bool bUniform)
{
	int32 NV = ResultMesh->MaxVertexID();

	// cache boundary verts info
	TArray<bool> bIsBoundary;
	TArray<int32> BoundaryVerts;
	bIsBoundary.SetNum(NV);
	ParallelFor(NV, [&](int32 vid)
	{
		bIsBoundary[vid] = ResultMesh->IsBoundaryVertex(vid) && ResultMesh->IsReferencedVertex(vid);
	});
	for (int32 vid = 0; vid < NV; ++vid)
	{
		if (bIsBoundary[vid])
		{
			BoundaryVerts.Add(vid);
		}
	}
	// have to limit boundary alpha to avoid oscillations because there are usually only 2 nbrs
	double ClampedBoundaryAlpha = FMathd::Lerp(0.0, 0.9, SmoothOptions.SmoothAlpha);


	for (int32 k = 0; k < SmoothOptions.Iterations; ++k)
	{
		// calculate smoothed positions of interior vertices
		ParallelFor(NV, [&](int32 vid) 
		{
			if (ResultMesh->IsReferencedVertex(vid) == false || bIsBoundary[vid])
			{
				SmoothedBuffer[vid] = PositionBuffer[vid];
				return;
			}

			FVector3d Centroid;
			if (bUniform)
			{
				Centroid = FMeshWeights::UniformCentroid(*ResultMesh, vid, [&](int32 nbrvid) { return PositionBuffer[nbrvid]; });
			}
			else
			{
				FVector3d Uniform = FMeshWeights::UniformCentroid(*ResultMesh, vid, [&](int32 nbrvid) { return PositionBuffer[nbrvid]; });
				FVector3d MeanCurvNorm = UE::MeshCurvature::MeanCurvatureNormal(*ResultMesh, vid, [&](int32 nbrvid) { return PositionBuffer[nbrvid]; });
				Centroid = PositionBuffer[vid] - 0.5*MeanCurvNorm;
				if (Centroid.DistanceSquared(PositionBuffer[vid]) > Uniform.DistanceSquared(PositionBuffer[vid]))
				{
					Centroid = Uniform;
				}
			}

			SmoothedBuffer[vid] = FVector3d::Lerp(PositionBuffer[vid], Centroid, SmoothOptions.SmoothAlpha);

		} /*, EParallelForFlags::ForceSingleThread*/ );

		// calculate boundary vertices
		if (SmoothOptions.bSmoothBoundary)
		{
			ParallelFor(BoundaryVerts.Num(), [=](int32 idx)
			{
				int32 vid = BoundaryVerts[idx];
				FVector3d Centroid(0, 0, 0);
				int32 NumNbrs = 0;
				for (int32 nbrvid : ResultMesh->VtxVerticesItr(vid))
				{
					if (bIsBoundary[nbrvid])
					{
						Centroid += PositionBuffer[nbrvid];
						NumNbrs++;
					}
				}
				Centroid = (NumNbrs > 0) ? (Centroid / (double)NumNbrs) : PositionBuffer[vid];
				SmoothedBuffer[vid] = FVector3d::Lerp(PositionBuffer[vid], Centroid, ClampedBoundaryAlpha);
			});
		}

		for (int32 vid = 0; vid < NV; ++vid)
		{
			PositionBuffer[vid] = SmoothedBuffer[vid];
		}
	}
}
