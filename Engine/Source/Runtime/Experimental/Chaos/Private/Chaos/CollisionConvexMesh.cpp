// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/CollisionConvexMesh.h"
#include "HAL/IConsoleManager.h"

namespace Chaos
{
	// CVars variables for controlling geometry complexity checking and simplification
	template <>
	int32 TConvexBuilder<float>::PerformGeometryCheck = 0;

	template <>
	int32 TConvexBuilder<float>::PerformGeometryReduction = 0;

	template <>
	int32 TConvexBuilder<float>::ParticlesThreshold = 50;

	FAutoConsoleVariableRef CVarConvexGeometryCheckEnable(TEXT("p.Chaos.ConvexGeometryCheckEnable"), TConvexBuilder<float>::PerformGeometryCheck, TEXT("Perform convex geometry complexity check for Chaos physics."));

	FAutoConsoleVariableRef CVarConvexGeometrySimplifyEnable(TEXT("p.Chaos.PerformGeometryReduction"), TConvexBuilder<float>::PerformGeometryReduction, TEXT("Perform convex geometry simplification to increase performance in Chaos physics."));

	FAutoConsoleVariableRef CVarConvexParticlesWarningThreshold(TEXT("p.Chaos.ConvexParticlesWarningThreshold"), TConvexBuilder<float>::ParticlesThreshold, TEXT("Threshold beyond which we warn about collision geometry complexity."));
}