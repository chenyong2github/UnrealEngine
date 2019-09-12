// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SmoothingOps/MeanValueSmoothingOp.h"

#include "MeshSmoothingUtilities.h"


FMeanValueSmoothingOp::FMeanValueSmoothingOp(const FDynamicMesh3* Mesh, float Speed, int32 Iterations) :
	FSmoothingOpBase(Mesh, Speed, Iterations)
{
}


void FMeanValueSmoothingOp::CalculateResult(FProgressCancel* Progress) 
{
	// Update the values in the position buffer with smoothed positions.
	Smooth();

	// Copy the results back into the result mesh and update normals
	UpdateResultMesh();

}

void FMeanValueSmoothingOp::Smooth()
{
	double Intensity = 1.;
	MeshSmoothingOperators::ComputeSmoothing_BiHarmonic(ELaplacianWeightScheme::MeanValue, *ResultMesh, SmoothSpeed, Intensity, SmoothIterations, PositionBuffer);
}