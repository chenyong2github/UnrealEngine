// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomCreateBindingOptions.h"

UGroomCreateBindingOptions::UGroomCreateBindingOptions(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
	GroomBindingType = EGroomBindingMeshType::SkeletalMesh;
	SourceSkeletalMesh = nullptr;
	TargetSkeletalMesh = nullptr;
	SourceGeometryCache = nullptr;
	TargetGeometryCache = nullptr;
	NumInterpolationPoints = 100;
	MatchingSection = 0;
}
