// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmoothingOps/IterativeSmoothingOp.h"
#include "Async/ParallelFor.h"

FIterativeSmoothingOp::FIterativeSmoothingOp(const FDynamicMesh3* Mesh, float Speed, int32 Iterations) :
	FSmoothingOpBase(Mesh, Speed, Iterations)
{
	SmoothedBuffer = PositionBuffer;
}

void FIterativeSmoothingOp::CalculateResult(FProgressCancel* Progress)
{
	// Update the values in the position buffer with smoothed positions.
	Smooth();

	// Copy the results back into the result mesh and update normals
	UpdateResultMesh();

}

void FIterativeSmoothingOp::Smooth()
{

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
	// 
	// NB: Here -w_{ii} is the valence of the ith vertex.  
	//     and 'k' may be spatially dependent. 

	int NV = ResultMesh->MaxVertexID();

	for (int k = 0; k < SmoothIterations; ++k)
	{
		ParallelFor(NV, [=](int vid) {
			if (ResultMesh->IsReferencedVertex(vid) == false)
			{
				return;
			}

			FVector3d Centroid(0, 0, 0);
			int NumNbrs = 0;
			for (int nbrvid : ResultMesh->VtxVerticesItr(vid))
			{
				Centroid += PositionBuffer[nbrvid];
				NumNbrs++;
			}
			Centroid /= (double)NumNbrs;
			SmoothedBuffer[vid] = FVector3d::Lerp(PositionBuffer[vid], Centroid, SmoothSpeed);
		}, false);

		for (int vid = 0; vid < NV; ++vid)
		{
			PositionBuffer[vid] = SmoothedBuffer[vid];
		}
	}
}
