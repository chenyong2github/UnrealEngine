// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "Dataflow/DataflowEngine.h"
#include "Dataflow/DataflowSelection.h"
#include "GeometryCollection/ManagedArrayCollection.h"

#include "GeometryCollectionUtilityNodes.generated.h"


UENUM(BlueprintType)
enum class EConvexOverlapRemovalMethodEnum : uint8
{
	Dataflow_EConvexOverlapRemovalMethod_None UMETA(DisplayName = "None"),
	Dataflow_EConvexOverlapRemovalMethod_All UMETA(DisplayName = "All"),
	Dataflow_EConvexOverlapRemovalMethod_OnlyClusters UMETA(DisplayName = "Only Clusters"),
	Dataflow_EConvexOverlapRemovalMethod_OnlyClustersVsClusters UMETA(DisplayName = "Only Clusters vs Clusters"),
	//~~~
	//256th entry
	Dataflow_Max                UMETA(Hidden)
};

/**
 *
 * Generates convex hull representation for the bones for simulation
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FCreateNonOverlappingConvexHullsDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCreateNonOverlappingConvexHullsDataflowNode, "CreateNonOverlappingConvexHulls", "GeometryCollection|Utilities", "")

public:
	UPROPERTY(meta = (DataflowInput, DataflowOutput))
	FManagedArrayCollection Collection;

	UPROPERTY(EditAnywhere, Category = "Convex", meta = (DataflowInput, UIMin = 0.01f, UIMax = 1.f))
	float CanRemoveFraction = 0.3f;

	UPROPERTY(EditAnywhere, Category = "Convex", meta = (DataflowInput, UIMin = 0.f))
	float SimplificationDistanceThreshold = 0.f;

	UPROPERTY(EditAnywhere, Category = "Convex", meta = (DataflowInput, UIMin = 0.f))
	float CanExceedFraction = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Convex")
	EConvexOverlapRemovalMethodEnum OverlapRemovalMethod = EConvexOverlapRemovalMethodEnum::Dataflow_EConvexOverlapRemovalMethod_All;

	UPROPERTY(EditAnywhere, Category = "Convex", meta = (DataflowInput, UIMin = 0.f, UIMax = 100.f))
	float OverlapRemovalShrinkPercent = 0.f;

	FCreateNonOverlappingConvexHullsDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};

/**
 *
 * Generates cluster convex hulls for leafs hulls
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FGenerateClusterConvexHullsFromLeafHullsDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FGenerateClusterConvexHullsFromLeafHullsDataflowNode, "GenerateClusterConvexHullsFromLeafHulls", "GeometryCollection|Utilities", "")

public:
	UPROPERTY(meta = (DataflowInput, DataflowOutput))
	FManagedArrayCollection Collection;

	/** Maximum number of convex to generate for a specific cluster. Will be ignored if error tolerance is used instead */
	UPROPERTY(EditAnywhere, Category = "Convex", meta = (DataflowInput, EditCondition = "ErrorTolerance == 0"))
	int32 ConvexCount = 2;
	
	/** 
	* Error tolerance to use to decide to merge leaf convex together. 
	* This is in centimeters and represents the side of a cube, the volume of which will be used as threshold
	* to know if the volume of the generated convex is too large compared to the sum of the volume of the leaf convex
	*/
	UPROPERTY(EditAnywhere, Category = "Convex", meta = (DataflowInput, UIMin = "0", UIMax = "100.", Units = cm))
	double ErrorTolerance = 0.0;

	/** Optional transform selection to compute cluster hulls on -- if not provided, all cluster hulls will be computed. */
	UPROPERTY(meta = (DataflowInput, DataflowIntrinsic))
	FDataflowTransformSelection OptionalSelectionFilter;

	FGenerateClusterConvexHullsFromLeafHullsDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};

/**
 *
 * Generates cluster convex hulls for children hulls
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FGenerateClusterConvexHullsFromChildrenHullsDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FGenerateClusterConvexHullsFromChildrenHullsDataflowNode, "GenerateClusterConvexHullsFromChildrenHullsDataflowNode", "GeometryCollection|Utilities", "")

public:
	UPROPERTY(meta = (DataflowInput, DataflowOutput))
	FManagedArrayCollection Collection;

	/** Maximum number of convex to generate for a specific cluster. Will be ignored if error tolerance is used instead */
	UPROPERTY(EditAnywhere, Category = "Convex", meta = (DataflowInput, EditCondition = "ErrorTolerance == 0"))
	int32 ConvexCount = 2;

	/**
	* Error tolerance to use to decide to merge leaf convex together.
	* This is in centimeters and represents the side of a cube, the volume of which will be used as threshold
	* to know if the volume of the generated convex is too large compared to the sum of the volume of the leaf convex
	*/
	UPROPERTY(EditAnywhere, Category = "Convex", meta = (DataflowInput, UIMin = "0", UIMax = "100.", Units = cm))
	double ErrorTolerance = 0.0;

	/** Optional transform selection to compute cluster hulls on -- if not provided, all cluster hulls will be computed. */
	UPROPERTY(meta = (DataflowInput, DataflowIntrinsic))
	FDataflowTransformSelection OptionalSelectionFilter;

	FGenerateClusterConvexHullsFromChildrenHullsDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


namespace Dataflow
{
	void GeometryCollectionUtilityNodes();
}

