// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/EnumClassFlags.h"
#include "Sampling/MeshImageBaker.h"
#include "Sampling/MeshImageBakingCache.h"
#include "Image/ImageBuilder.h"
#include "MeshTangents.h"
#include "Math/RandomStream.h"

namespace UE
{
namespace Geometry
{

enum class EOcclusionMapType : uint8
{
	None = 0,
	AmbientOcclusion = 1,
	BentNormal = 2,
	All = 3
};
ENUM_CLASS_FLAGS(EOcclusionMapType);

class DYNAMICMESH_API FMeshOcclusionMapBaker : public FMeshImageBaker
{
public:
	virtual ~FMeshOcclusionMapBaker() {}

	//
	// Inputs
	//

	const TMeshTangents<double>* BaseMeshTangents = nullptr;

	//
	// Options
	//

	enum class EDistribution
	{
		Uniform,
		Cosine
	};

	enum class ESpace
	{
		Tangent,
		Object
	};

	EOcclusionMapType OcclusionType = EOcclusionMapType::All;
	int32 NumOcclusionRays = 32;
	double MaxDistance = TNumericLimits<double>::Max();
	double SpreadAngle = 180.0;
	EDistribution Distribution = EDistribution::Cosine;

	// Ambient Occlusion
	double BiasAngleDeg = 15.0;
	double BlurRadius = 0.0;

	// Bent Normal
	ESpace NormalSpace = ESpace::Tangent;


	//
	// Compute functions
	//

	virtual void Bake() override;

	//
	// Output
	//

	enum class EResult
	{
		AmbientOcclusion,
		BentNormal
	};

	const TUniquePtr<TImageBuilder<FVector3f>>& GetResult(EResult Result) const
	{
		switch (Result)
		{
		case EResult::AmbientOcclusion:
			return OcclusionBuilder;
		case EResult::BentNormal:
			return NormalBuilder;
		default:
			check(false);
			return OcclusionBuilder;
		}
	}

	TUniquePtr<TImageBuilder<FVector3f>> TakeResult(EResult Result)
	{
		switch (Result)
		{
		case EResult::AmbientOcclusion:
			return MoveTemp(OcclusionBuilder);
		case EResult::BentNormal:
			return MoveTemp(NormalBuilder);
		default:
			check(false);
			return MoveTemp(OcclusionBuilder);
		}
	}

	//
	// Utility
	//

	inline bool WantAmbientOcclusion() const
	{
		return ((OcclusionType & EOcclusionMapType::AmbientOcclusion) == EOcclusionMapType::AmbientOcclusion);
	}

	inline bool WantBentNormal() const
	{
		return ((OcclusionType & EOcclusionMapType::BentNormal) == EOcclusionMapType::BentNormal);
	}

protected:
	TUniquePtr<TImageBuilder<FVector3f>> OcclusionBuilder;
	TUniquePtr<TImageBuilder<FVector3f>> NormalBuilder;

public:
	//
	// New Baker interface
	//

	/** Invoked at start of bake to initialize baker. */
	virtual void PreEvaluate(const FMeshMapBaker& Baker) override;

	/** Evaluate the sample at this mesh correspondence point. */
	virtual FVector4f EvaluateSample(const FMeshMapBaker& Baker, const FCorrespondenceSample& Sample) override;

	/** @return the default sample value for the baker. */
	virtual FVector4f DefaultSample() const override
	{
		if (WantAmbientOcclusion())
		{
			return FVector4f::One();
		}
		else //if (WantBentNormal())
		{
			FVector3f DefaultNormal = FVector3f::UnitZ();
			switch (NormalSpace)
			{
			case ESpace::Tangent:
				break;
			case ESpace::Object:
				DefaultNormal = FVector3f::Zero();
				break;
			}
			// Map normal space [-1,1] to floating point color space [0,1]
			DefaultNormal = (DefaultNormal + FVector3f::One()) * 0.5f;
			return FVector4f(DefaultNormal.X, DefaultNormal.Y, DefaultNormal.Z, 1.0f);
		}
	}

protected:
	const FDynamicMesh3* DetailMesh = nullptr;
	const FDynamicMeshAABBTree3* DetailSpatial = nullptr;
	const FDynamicMeshNormalOverlay* DetailNormalOverlay = nullptr;
	double BiasDotThreshold = 0.25;
	TArray<FVector3d> RayDirections;

	FRandomStream RotationGen = FRandomStream(31337);
	FCriticalSection RotationLock;

private:
	template<class SampleType>
	void SampleFunction(const SampleType& Sample, double& Occlusion, FVector3d& Normal);

	double GetRandomRotation();
};

} // end namespace UE::Geometry
} // end namespace UE
