// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Sampling/MeshMapEvaluator.h"

namespace UE
{
namespace Geometry
{

enum class EMeshPropertyMapType
{
	Position = 1,
	Normal = 2,
	FacetNormal = 3,
	UVPosition = 4,
	MaterialID = 5
};

class DYNAMICMESH_API FMeshPropertyMapEvaluator : public FMeshMapEvaluator
{
public:
	EMeshPropertyMapType Property = EMeshPropertyMapType::Normal;

public:
	/** Invoked at start of bake to initialize baker. */
	virtual void Setup(const FMeshMapBaker& Baker, FEvaluationContext& Context) override;

	static void EvaluateSample(float*& Out, const FCorrespondenceSample& Sample, void* EvalData);

	static void EvaluateDefault(float*& Out, void* EvalData);

protected:
	// Cached data
	const FDynamicMesh3* DetailMesh = nullptr;
	const FDynamicMeshNormalOverlay* DetailNormalOverlay = nullptr;
	const FDynamicMeshUVOverlay* DetailUVOverlay = nullptr;
	FAxisAlignedBox3d Bounds;
	FVector3f DefaultValue = FVector3f::Zero();

private:
	FVector3f NormalToColor(const FVector3d Normal)
	{
		return (FVector3f)((Normal + FVector3d::One()) * 0.5);
	}

	FVector3f UVToColor(const FVector2d UV)
	{
		double X = FMathd::Clamp(UV.X, 0.0, 1.0);
		double Y = FMathd::Clamp(UV.Y, 0.0, 1.0);
		return (FVector3f)FVector3d(X, Y, 0);
	}

	FVector3f PositionToColor(const FVector3d Position, const FAxisAlignedBox3d SafeBounds)
	{
		double X = (Position.X - SafeBounds.Min.X) / SafeBounds.Width();
		double Y = (Position.Y - SafeBounds.Min.Y) / SafeBounds.Height();
		double Z = (Position.Z - SafeBounds.Min.Z) / SafeBounds.Depth();
		return (FVector3f)FVector3d(X, Y, Z);
	}

	FVector3f SampleFunction(const FCorrespondenceSample& Sample);
};

} // end namespace UE::Geometry
} // end namespace UE

