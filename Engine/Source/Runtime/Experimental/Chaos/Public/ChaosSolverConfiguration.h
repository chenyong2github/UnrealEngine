// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SolverEventFilters.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "PBDRigidsSolver.h"
#include "ChaosSolverConfiguration.generated.h"

UENUM()
enum class EClusterUnionMethod : uint8
{
	PointImplicit,
	DelaunayTriangulation,
	MinimalSpanningSubsetDelaunayTriangulation,
	PointImplicitAugmentedWithMinimalDelaunay,
	None
};

USTRUCT()
struct FChaosSolverConfiguration
{
	GENERATED_BODY();

	FChaosSolverConfiguration()
		: Iterations(FEvolution::DefaultNumIterations)
		, CollisionPairIterations(FEvolution::DefaultNumCollisionPairIterations)
		, PushOutIterations(FEvolution::DefaultNumPushOutIterations)
		, CollisionPushOutPairIterations(FEvolution::DefaultNumCollisionPushOutPairIterations)
		, CollisionMarginFraction(FEvolution::DefaultCollisionMarginFraction)
		, CollisionMarginMax(FEvolution::DefaultCollisionMarginMax)
		, CollisionCullDistance(FEvolution::DefaultCollisionCullDistance)
		, JointPairIterations(FEvolution::DefaultNumJointPairIterations)
		, JointPushOutPairIterations(FEvolution::DefaultNumJointPushOutPairIterations)
		, ClusterConnectionFactor(1.0f)
		, ClusterUnionConnectionType(EClusterUnionMethod::DelaunayTriangulation)
		, bGenerateCollisionData(false)
		, bGenerateBreakData(false)
		, bGenerateTrailingData(false)
		, bGenerateContactGraph(true)
	{
	}

	// The number of iterations to run during the constraint solver step
	UPROPERTY(EditAnywhere, Category = "SolverConfiguration|Iterations")
	int32 Iterations;

	// During solver iterations we solve each constraint in turn. For each constraint
	// we run the solve step CollisionPairIterations times in a row.
	UPROPERTY(EditAnywhere, Category = "SolverConfiguration|Iterations")
	int32 CollisionPairIterations;

	// The number of iterations to run during the constraint fixup step. This applies a post-solve
	// correction that can address errors left behind during the main solver iterations.
	UPROPERTY(EditAnywhere, Category = "SolverConfiguration|Iterations")
	int32 PushOutIterations;

	// During pushout iterations we pushout each constraint in turn. For each constraint
	// we run the pushout step CollisionPairIterations times in a row.
	UPROPERTY(EditAnywhere, Category = "SolverConfiguration|Iterations")
	int32 CollisionPushOutPairIterations;

	// A collision margin as a fraction of size used by some boxes and convex shapes to improve collision detection results.
	// The core geometry of shapes that support a margin are reduced in size by the margin, and the margin
	// is added back on during collision detection. The net result is a shape of the same size but with rounded corners.
	UPROPERTY(EditAnywhere, Category = "SolverConfiguration|Collision")
	float CollisionMarginFraction;

	// An upper limit on the collision margin that will be subtracted from boxes and convex shapes. See CollisionMarginFraction
	UPROPERTY(EditAnywhere, Category = "SolverConfiguration|Collision")
	float CollisionMarginMax;

	// During collision detection, if tweo shapes are at least this far apart we do not calculate their nearest features
	// during the collision detection step.
	UPROPERTY(EditAnywhere, Category = "SolverConfiguration|Collision")
	float CollisionCullDistance;

	// The number of iterations to run on each constraint during the constraint solver step
	UPROPERTY(EditAnywhere, Category = "SolverConfiguration|Iterations")
	int32 JointPairIterations;

	// The number of iterations to run during the constraint fixup step for each joint. This applies a post-solve
	// correction that can address errors left behind during the main solver iterations.
	UPROPERTY(EditAnywhere, Category = "SolverConfiguration|Iterations")
	int32 JointPushOutPairIterations;


	UPROPERTY(EditAnywhere, Category = "SolverConfiguration|Clustering")
	float ClusterConnectionFactor;

	UPROPERTY(EditAnywhere, Category = "SolverConfiguration|Clustering")
	EClusterUnionMethod ClusterUnionConnectionType;

	UPROPERTY(EditAnywhere, Category = "SolverConfiguration|GeneratedData")
	bool bGenerateCollisionData;

	UPROPERTY(EditAnywhere, Category = "SolverConfiguration|GeneratedData", meta=(EditCondition=bGenerateCollisionData))
	FSolverCollisionFilterSettings CollisionFilterSettings;

	UPROPERTY(EditAnywhere, Category = "SolverConfiguration|GeneratedData")
	bool bGenerateBreakData;

	UPROPERTY(EditAnywhere, Category = "SolverConfiguration|GeneratedData", meta = (EditCondition = bGenerateBreakData))
	FSolverBreakingFilterSettings BreakingFilterSettings;

	UPROPERTY(EditAnywhere, Category = "SolverConfiguration|GeneratedData")
	bool bGenerateTrailingData;

	UPROPERTY(EditAnywhere, Category = "SolverConfiguration|GeneratedData", meta = (EditCondition = bGenerateTrailingData))
	FSolverTrailingFilterSettings TrailingFilterSettings;

	UPROPERTY(EditAnywhere, Category = "SolverConfiguration|Contacts")
	bool bGenerateContactGraph;

private:
	using FEvolution = Chaos::TPBDRigidsEvolutionGBF<Chaos::FDefaultTraits>;
};