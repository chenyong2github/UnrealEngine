// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmoothingOps/CotanSmoothingOp.h"
#include "Solvers/MeshSmoothing.h"
#include "Solvers/ConstrainedMeshSmoother.h"
#include "MeshCurvature.h"
#include "MeshWeights.h"


FCotanSmoothingOp::FCotanSmoothingOp(const FDynamicMesh3* Mesh, const FSmoothingOpBase::FOptions& OptionsIn) :
	FSmoothingOpBase(Mesh, OptionsIn)
{
}


void FCotanSmoothingOp::CalculateResult(FProgressCancel* Progress)
{
	// Update the values in the position buffer with smoothed positions.
	Smooth();

	// Copy the results back into the result mesh and update normals
	UpdateResultMesh();

}

void FCotanSmoothingOp::Smooth()
{
	ELaplacianWeightScheme UseScheme = ELaplacianWeightScheme::ClampedCotangent;
	if (SmoothOptions.bUniform)
	{
		UseScheme = ELaplacianWeightScheme::Uniform;
	}

	TUniquePtr<IConstrainedMeshSolver> Smoother = UE::MeshDeformation::ConstructConstrainedMeshSmoother(
		UseScheme, *ResultMesh);

	double Power = SmoothOptions.SmoothPower;
	if (Power < 0.0001)
	{
		for (int32 vid : ResultMesh->VertexIndicesItr())
		{
			PositionBuffer[vid] = ResultMesh->GetVertex(vid);
		}
	}
	else if ( Power > 10000 )
	{
		Smoother->Deform(PositionBuffer);
	}
	else
	{
		double Weight = 1.0 / Power;
		for (int32 vid : ResultMesh->VertexIndicesItr())
		{
			Smoother->AddConstraint(vid, Weight, ResultMesh->GetVertex(vid), false);
		}
		Smoother->Deform(PositionBuffer);
	}

}
