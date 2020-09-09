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
		: CollisionIterations(FEvolution::DefaultNumIterations)
		, PushOutIterations(FEvolution::DefaultNumPushOutIterations)
		, PushOutPairIterations(FEvolution::DefaultNumPushOutPairIterations)
		, ClusterConnectionFactor(1.0f)
		, ClusterUnionConnectionType(EClusterUnionMethod::DelaunayTriangulation)
		, bGenerateCollisionData(false)
		, bGenerateBreakData(false)
		, bGenerateTrailingData(false)
		, bGenerateContactGraph(true)
	{
		// Take CVar overrides from the solver
		CollisionIterations = ChaosSolverCollisionDefaultIterationsCVar;
		PushOutIterations = ChaosSolverCollisionDefaultPushoutIterationsCVar;
	}

	UPROPERTY(EditAnywhere, Category = "SolverConfiguration|Iterations")
	int32 CollisionIterations;

	UPROPERTY(EditAnywhere, Category = "SolverConfiguration|Iterations")
	int32 PushOutIterations;

	UPROPERTY(EditAnywhere, Category = "SolverConfiguration|Iterations")
	int32 PushOutPairIterations;

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