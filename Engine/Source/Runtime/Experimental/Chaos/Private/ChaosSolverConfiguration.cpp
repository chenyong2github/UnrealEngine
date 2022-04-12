// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosSolverConfiguration.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"

using FEvolution = Chaos::FPBDRigidsEvolutionGBF;

FChaosSolverConfiguration::FChaosSolverConfiguration()
	: Iterations(FEvolution::DefaultNumIterations)
	, CollisionPairIterations(FEvolution::DefaultNumCollisionPairIterations)
	, PushOutIterations(FEvolution::DefaultNumPushOutIterations)
	, CollisionPushOutPairIterations(FEvolution::DefaultNumCollisionPushOutPairIterations)
	, CollisionMarginFraction(FEvolution::DefaultCollisionMarginFraction)
	, CollisionMarginMax(FEvolution::DefaultCollisionMarginMax)
	, CollisionCullDistance(FEvolution::DefaultCollisionCullDistance)
	, CollisionMaxPushOutVelocity(FEvolution::DefaultCollisionMaxPushOutVelocity)
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
