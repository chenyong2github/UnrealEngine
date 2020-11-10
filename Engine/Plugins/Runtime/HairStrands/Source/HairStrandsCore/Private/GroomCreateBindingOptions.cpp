// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomCreateBindingOptions.h"

UGroomCreateBindingOptions::UGroomCreateBindingOptions(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
	SourceSkeletalMesh = nullptr;
	TargetSkeletalMesh = nullptr;
	NumInterpolationPoints = 100;
}
