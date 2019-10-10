// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SmoothingOps/CotanSmoothingOp.h"

#include "MeshSmoothingUtilities.h"


FCotanSmoothingOp::FCotanSmoothingOp(const FDynamicMesh3* Mesh, float Speed, int32 Iterations) :
	FSmoothingOpBase(Mesh, Speed, Iterations)
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
	double Intensity = 1.;
	MeshSmoothingOperators::ComputeSmoothing_BiHarmonic(ELaplacianWeightScheme::ClampedCotangent, *ResultMesh, SmoothSpeed, Intensity, SmoothIterations, PositionBuffer);
}
