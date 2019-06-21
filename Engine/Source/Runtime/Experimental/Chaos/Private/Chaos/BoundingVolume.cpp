// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/BoundingVolume.h"

int FBoundingVolumeCVars::FilterFarBodies = 1;

FAutoConsoleVariableRef FBoundingVolumeCVars::CVarFilterFarBodies(
	TEXT("p.RemoveFarBodiesFromBVH"),
	FBoundingVolumeCVars::FilterFarBodies,
	TEXT("Removes bodies far from the scene from the bvh\n")
	TEXT("0: Kept, 1: Removed"),
	ECVF_Default);