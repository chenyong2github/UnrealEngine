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


double FIterativeSmoothingOp::GetSmoothAlpha(int32 VertexID, bool bIsBoundary)
{
	double UseAlpha = (bIsBoundary) ? SmoothOptions.BoundarySmoothAlpha : SmoothOptions.SmoothAlpha;
	if (SmoothOptions.bUseWeightMap)
	{
		double t = FMathd::Clamp(SmoothOptions.WeightMap->GetValue(VertexID), 0.0, 1.0);
		UseAlpha = FMathd::Lerp(SmoothOptions.WeightMapMinMultiplier * UseAlpha, UseAlpha, t);
	}
	return UseAlpha;
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
				Centroid = FMeshWeights::CotanCentroidSafe(*ResultMesh, vid, [&](int32 nbrvid) { return PositionBuffer[nbrvid]; }, 1.0);

				// This code does not work because the mean curvature increases as things get smaller. Need to normalize it,
				// however that *also* doesn't really work because there is a maximum step size based on edge length, and as edges
				// collapse to nearly zero length, progress stops. Need to refine while smoothing.

				//FVector3d Uniform = FMeshWeights::UniformCentroid(*ResultMesh, vid, [&](int32 nbrvid) { return PositionBuffer[nbrvid]; });
				//FVector3d MeanCurvNorm = UE::MeshCurvature::MeanCurvatureNormal(*ResultMesh, vid, [&](int32 nbrvid) { return PositionBuffer[nbrvid]; });
				//Centroid = PositionBuffer[vid] - 0.5*MeanCurvNorm;
				//if (Centroid.DistanceSquared(PositionBuffer[vid]) > Uniform.DistanceSquared(PositionBuffer[vid]))
				//{
				//	Centroid = Uniform;
				//}
			}

			double UseAlpha = GetSmoothAlpha(vid, false);
			SmoothedBuffer[vid] = FVector3d::Lerp(PositionBuffer[vid], Centroid, UseAlpha);

		} /*, EParallelForFlags::ForceSingleThread*/ );

		// calculate boundary vertices
		if (SmoothOptions.bSmoothBoundary)
		{
			ParallelFor(BoundaryVerts.Num(), [=](int32 idx)
			{
				int32 vid = BoundaryVerts[idx];
				FVector3d Centroid = FMeshWeights::FilteredUniformCentroid(*ResultMesh, vid,
					[&](int32 nbrvid) { return PositionBuffer[nbrvid]; },
					[&](int32 nbrvid) { return bIsBoundary[nbrvid]; });
				double UseAlpha = GetSmoothAlpha(vid, true);
				SmoothedBuffer[vid] = FVector3d::Lerp(PositionBuffer[vid], Centroid, UseAlpha);
			});
		}

		for (int32 vid = 0; vid < NV; ++vid)
		{
			PositionBuffer[vid] = SmoothedBuffer[vid];
		}
	}
}
