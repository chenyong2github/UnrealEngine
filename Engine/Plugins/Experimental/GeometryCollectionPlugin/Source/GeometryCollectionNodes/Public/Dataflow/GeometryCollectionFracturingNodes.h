// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "Dataflow/DataflowEngine.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "Dataflow/DataflowSelection.h"

#include "GeometryCollectionFracturingNodes.generated.h"

class FGeometryCollection;


USTRUCT(meta = (DataflowGeometryCollection))
struct FUniformScatterPointsDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FUniformScatterPointsDataflowNode, "UniformScatterPoints", "Generators|Point", "")

public:
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput));
	int32 MinNumberOfPoints = 20;

	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput));
	int32 MaxNumberOfPoints = 20;

	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput));
	float RandomSeed = -1.f;

	UPROPERTY(meta = (DataflowInput))
	FBox BoundingBox = FBox(ForceInit);

	UPROPERTY(meta = (DataflowOutput))
	TArray<FVector> Points;

	FUniformScatterPointsDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&BoundingBox);
		RegisterInputConnection(&MinNumberOfPoints);
		RegisterInputConnection(&MaxNumberOfPoints);
		RegisterInputConnection(&RandomSeed);
		RegisterOutputConnection(&Points);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


USTRUCT(meta = (DataflowGeometryCollection))
struct FRadialScatterPointsDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FRadialScatterPointsDataflowNode, "RadialScatterPoints", "Generators|Point", "")

public:
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput));
	FVector Center = FVector(0.0);

	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput));
	FVector Normal = FVector(0.0, 0.0, 1.0);

	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput, UIMin = 0.01f));
	float Radius = 50.f;

	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput, UIMin = 1, UIMax = 50));
	int32 AngularSteps = 5;

	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput, UIMin = 1, UIMax = 50));
	int32 RadialSteps = 5;

	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput));
	float AngleOffset = 0.f;

	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput, UIMin = 0.f));
	float Variability = 0.f;

	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (DataflowInput));
	float RandomSeed = -1.f;

	UPROPERTY(meta = (DataflowOutput))
	TArray<FVector> Points;

	FRadialScatterPointsDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Center);
		RegisterInputConnection(&Normal);
		RegisterInputConnection(&Radius);
		RegisterInputConnection(&AngularSteps);
		RegisterInputConnection(&RadialSteps);
		RegisterInputConnection(&AngleOffset);
		RegisterInputConnection(&Variability);
		RegisterInputConnection(&RandomSeed);
		RegisterOutputConnection(&Points);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


//
// GridScatterPoints
//


/**
 *
 * Generates a Voronoi fracture
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FVoronoiFractureDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FVoronoiFractureDataflowNode, "VoronoiFracture", "GeometryCollection|Fracture", "")

public:
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	UPROPERTY(meta = (DataflowInput, DataflowIntrinsic))
	TArray<FVector> Points;

	UPROPERTY(EditAnywhere, Category = "Fracture", meta = (DataflowInput));
	float RandomSeed = -1.f;

	UPROPERTY(EditAnywhere, Category = "Fracture", meta = (DataflowInput, UIMin = 0.f, UIMax = 1.f));
	float ChanceToFracture = 1.f;

	UPROPERTY(EditAnywhere, Category = "Fracture");
	bool GroupFracture = true;

	UPROPERTY(EditAnywhere, Category = "Fracture", meta = (DataflowInput, UIMin = 0.f));
	float Grout = 0.f;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float Amplitude = 0.f;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.00001f));
	float Frequency = 0.1f;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float Persistence = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float Lacunarity = 2.f;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	int32 OctaveNumber = 4;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float PointSpacing = 10.f;

	UPROPERTY(EditAnywhere, Category = "Collision");
	bool AddSamplesForCollision = false;

	UPROPERTY(EditAnywhere, Category = "Collision", meta = (DataflowInput, UIMin = 0.f));
	float CollisionSampleSpacing = 50.f;

	FVoronoiFractureDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&Points);
		RegisterInputConnection(&RandomSeed);
		RegisterInputConnection(&ChanceToFracture);
		RegisterInputConnection(&Grout);
		RegisterInputConnection(&Amplitude);
		RegisterInputConnection(&Frequency);
		RegisterInputConnection(&Persistence);
		RegisterInputConnection(&Lacunarity);
		RegisterInputConnection(&OctaveNumber);
		RegisterInputConnection(&PointSpacing);
		RegisterInputConnection(&CollisionSampleSpacing);
		RegisterOutputConnection(&Collection, &Collection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Cuts geometry using a set of noised up planes
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FPlaneCutterDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
		DATAFLOW_NODE_DEFINE_INTERNAL(FPlaneCutterDataflowNode, "PlaneCutter", "GeometryCollection|Fracture", "")

public:
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	UPROPERTY(meta = (DataflowInput, DataflowIntrinsic))
	FBox BoundingBox = FBox(ForceInit);

	UPROPERTY(EditAnywhere, Category = "Fracture", meta = (DataflowInput, UIMin = 1))
	int32 NumPlanes = 1;

	UPROPERTY(EditAnywhere, Category = "Fracture", meta = (DataflowInput))
	float RandomSeed = -1.f;

	UPROPERTY(EditAnywhere, Category = "Fracture", meta = (DataflowInput, UIMin = 0.f))
	float Grout = 0.f;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float Amplitude = 0.f;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.00001f));
	float Frequency = 0.1f;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float Persistence = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float Lacunarity = 2.f;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	int32 OctaveNumber = 4;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (DataflowInput, UIMin = 0.f));
	float PointSpacing = 10.f;

	UPROPERTY(EditAnywhere, Category = "Collision");
	bool AddSamplesForCollision = false;

	UPROPERTY(EditAnywhere, Category = "Collision", meta = (DataflowInput, UIMin = 0.f));
	float CollisionSampleSpacing = 50.f;

	FPlaneCutterDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&BoundingBox);
		RegisterInputConnection(&NumPlanes);
		RegisterInputConnection(&RandomSeed);
		RegisterInputConnection(&Grout);
		RegisterInputConnection(&Amplitude);
		RegisterInputConnection(&Frequency);
		RegisterInputConnection(&Persistence);
		RegisterInputConnection(&Lacunarity);
		RegisterInputConnection(&OctaveNumber);
		RegisterInputConnection(&PointSpacing);
		RegisterInputConnection(&CollisionSampleSpacing);
		RegisterOutputConnection(&Collection, &Collection);
	}

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


namespace Dataflow
{
	void GeometryCollectionFracturingNodes();
}

