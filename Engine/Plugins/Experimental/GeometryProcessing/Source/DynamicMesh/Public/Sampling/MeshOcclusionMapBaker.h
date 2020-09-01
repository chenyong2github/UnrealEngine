// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "Sampling/MeshImageBaker.h"
#include "Sampling/MeshImageBakingCache.h"
#include "Image/ImageBuilder.h"


class DYNAMICMESH_API FMeshOcclusionMapBaker : public FMeshImageBaker
{
public:
	virtual ~FMeshOcclusionMapBaker() {}

	//
	// Options
	//

	int32 NumOcclusionRays = 32;
	double MaxDistance = TNumericLimits<double>::Max();
	double BiasAngleDeg = 15.0;

	double BlurRadius = 0.0;


	//
	// Compute functions
	//

	virtual void Bake() override;

	//
	// Output
	//

	const TUniquePtr<TImageBuilder<FVector3f>>& GetResult() const { return OcclusionBuilder; }

	TUniquePtr<TImageBuilder<FVector3f>> TakeResult() { return MoveTemp(OcclusionBuilder); }

protected:
	TUniquePtr<TImageBuilder<FVector3f>> OcclusionBuilder;

};