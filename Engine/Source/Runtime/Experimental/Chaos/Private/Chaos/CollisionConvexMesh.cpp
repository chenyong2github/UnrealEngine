// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/CollisionConvexMesh.h"
#include "HAL/IConsoleManager.h"

namespace Chaos
{
	// CVars variables for controlling geometry complexity checking and simplification 
	int32 FConvexBuilder::PerformGeometryCheck = 0;

	int32 FConvexBuilder::PerformGeometryReduction = 0;

	int32 FConvexBuilder::VerticesThreshold = 50;

	int32 FConvexBuilder::ComputeHorizonEpsilonFromMeshExtends = 1;

	FAutoConsoleVariableRef CVarConvexGeometryCheckEnable(TEXT("p.Chaos.ConvexGeometryCheckEnable"), FConvexBuilder::PerformGeometryCheck, TEXT("Perform convex geometry complexity check for Chaos physics."));

	FAutoConsoleVariableRef CVarConvexGeometrySimplifyEnable(TEXT("p.Chaos.PerformGeometryReduction"), FConvexBuilder::PerformGeometryReduction, TEXT("Perform convex geometry simplification to increase performance in Chaos physics."));

	FAutoConsoleVariableRef CVarConvexParticlesWarningThreshold(TEXT("p.Chaos.ConvexParticlesWarningThreshold"), FConvexBuilder::VerticesThreshold, TEXT("Threshold beyond which we warn about collision geometry complexity."));

	FAutoConsoleVariableRef CVarComputeHorizonEpsilonFromMeshExtends(TEXT("p.Chaos.ComputeHorizonEpsilonFromMeshExtends"), FConvexBuilder::ComputeHorizonEpsilonFromMeshExtends, TEXT("Controls whether or not we are using the extend of the mesh to compute the HorizonEpsilon or is we use an hardcoded one."));
}
