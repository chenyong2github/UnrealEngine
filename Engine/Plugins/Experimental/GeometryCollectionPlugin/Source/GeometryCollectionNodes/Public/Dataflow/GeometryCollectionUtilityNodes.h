// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "Dataflow/DataflowEngine.h"
#include "Dataflow/DataflowSelection.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "GeometryCollection/GeometryCollectionConvexUtility.h"

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


USTRUCT(meta = (DataflowGeometryCollection))
struct FCreateLeafConvexHullsDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCreateLeafConvexHullsDataflowNode, "CreateLeafConvexHulls", "GeometryCollection|Utilities", "")

public:
	UPROPERTY(meta = (DataflowInput, DataflowOutput))
	FManagedArrayCollection Collection;

	/** Optional transform selection to compute leaf hulls on -- if not provided, all leaf hulls will be computed. */
	UPROPERTY(meta = (DataflowInput, DataflowIntrinsic))
	FDataflowTransformSelection OptionalSelectionFilter;

	/** How convex hulls are generated -- computed from geometry, imported from external collision shapes, or an intersection of both options. */
	UPROPERTY(EditAnywhere, Category = Options)
	EGenerateConvexMethod GenerateMethod = EGenerateConvexMethod::ExternalCollision;

	/** If GenerateMethod is Intersect, only actually intersect when the volume of the Computed Hull is less than this fraction of the volume of the External Hull(s) */
	UPROPERTY(EditAnywhere, Category = IntersectionFilters, meta = (ClampMin = 0.0, ClampMax = 1.0, EditCondition = "GenerateMethod == EGenerateConvexMethod::IntersectExternalWithComputed"))
	float IntersectIfComputedIsSmallerByFactor = 1.0f;

	/** If GenerateMethod is Intersect, only actually intersect if the volume of the External Hull(s) exceed this threshold */
	UPROPERTY(EditAnywhere, Category = IntersectionFilters, meta = (ClampMin = 0.0, EditCondition = "GenerateMethod == EGenerateConvexMethod::IntersectExternalWithComputed"))
	float MinExternalVolumeToIntersect = 0.0f;

	/** Computed convex hulls are simplified to keep points spaced at least this far apart (except where needed to keep the hull from collapsing to zero volume) */
	UPROPERTY(EditAnywhere, Category = "Convex", meta = (DataflowInput, UIMin = 0.f, EditCondition = "GenerateMethod != EGenerateConvexMethod::ExternalCollision"))
	float SimplificationDistanceThreshold = 10.f;

	FCreateLeafConvexHullsDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
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

	/** Fraction (of geometry volume) by which a cluster's convex hull volume can exceed the actual geometry volume before instead using the hulls of the children.  0 means the convex volume cannot exceed the geometry volume; 1 means the convex volume is allowed to be 100% larger (2x) the geometry volume. */
	UPROPERTY(EditAnywhere, Category = Convex, meta = (DataflowInput, DisplayName = "Allow Larger Hull Fraction", ClampMin = 0.f))
	float CanExceedFraction = .5f;

	/** Computed convex hulls are simplified to keep points spaced at least this far apart (except where needed to keep the hull from collapsing to zero volume) */
	UPROPERTY(EditAnywhere, Category = Convex, meta = (DataflowInput, ClampMin = 0.f))
	float SimplificationDistanceThreshold = 10.f;

	/** Whether and in what cases to automatically cut away overlapping parts of the convex hulls, to avoid the simulation 'popping' to fix the overlaps */
	UPROPERTY(EditAnywhere, Category = AutomaticOverlapRemoval, meta = (DisplayName = "Remove Overlaps"))
	EConvexOverlapRemovalMethodEnum OverlapRemovalMethod = EConvexOverlapRemovalMethodEnum::Dataflow_EConvexOverlapRemovalMethod_All;

	/** Overlap removal will be computed as if convex hulls were this percentage smaller (in range 0-100) */
	UPROPERTY(EditAnywhere, Category = AutomaticOverlapRemoval, meta = (DataflowInput, ClampMin = 0.f, ClampMax = 99.9f))
	float OverlapRemovalShrinkPercent = 0.f;

	/** Fraction of the convex hulls for a cluster that we can remove before using the hulls of the children */
	UPROPERTY(EditAnywhere, Category = AutomaticOverlapRemoval, meta = (DataflowInput, DisplayName = "Max Removal Fraction", ClampMin = 0.01f, ClampMax = 1.f))
	float CanRemoveFraction = 0.3f;

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
	
	/** Whether to prefer available External (imported) collision shapes instead of the computed convex hulls on the Collection */
	UPROPERTY(EditAnywhere, Category = "Convex")
	bool bPreferExternalCollisionShapes = true;

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
	DATAFLOW_NODE_DEFINE_INTERNAL(FGenerateClusterConvexHullsFromChildrenHullsDataflowNode, "GenerateClusterConvexHullsFromChildrenHulls", "GeometryCollection|Utilities", "")

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
	
	/** Whether to prefer available External (imported) collision shapes instead of the computed convex hulls on the Collection */
	UPROPERTY(EditAnywhere, Category = "Convex")
	bool bPreferExternalCollisionShapes = true;

	/** Optional transform selection to compute cluster hulls on -- if not provided, all cluster hulls will be computed. */
	UPROPERTY(meta = (DataflowInput, DataflowIntrinsic))
	FDataflowTransformSelection OptionalSelectionFilter;

	FGenerateClusterConvexHullsFromChildrenHullsDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};

/**
 *
 * Update the Volume and Size attributes on the target Collection (and add them if they were not present)
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FUpdateVolumeAttributesDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FUpdateVolumeAttributesDataflowNode, "UpdateVolumeAttributes", "GeometryCollection|Utilities", "")

public:
	UPROPERTY(meta = (DataflowInput, DataflowOutput))
	FManagedArrayCollection Collection;

	FUpdateVolumeAttributesDataflowNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};


namespace Dataflow
{
	void GeometryCollectionUtilityNodes();
}

