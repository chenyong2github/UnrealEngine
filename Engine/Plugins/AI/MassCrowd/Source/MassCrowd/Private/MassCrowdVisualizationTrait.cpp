// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassCrowdVisualizationTrait.h"
#include "MassCrowdRepresentationSubsystem.h"
#include "MassCrowdVisualizationProcessor.h"

UMassCrowdVisualizationTrait::UMassCrowdVisualizationTrait()
{
	// Override the subsystem and deinitializer to support parallelization of the crowd
	RepresentationSubsystemClass = UMassCrowdRepresentationSubsystem::StaticClass();
	// @todo the following line will be cut once new de/initializers are in
	RepresentationFragmentDeinitializerClass = UMassCrowdRepresentationFragmentDestructor::StaticClass();
	RepresentationDestructorTag = FMassCrowdRepresentationDestructorTag::StaticStruct();
}
