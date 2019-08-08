// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionExample.h"
#include "GeometryCollection/GeometryCollectionExampleResponse.h"

#include "GeometryCollection/GeometryCollectionExampleClustering.h"
#include "GeometryCollection/GeometryCollectionExampleCreation.h"
#include "GeometryCollection/GeometryCollectionExampleDecimation.h"
#include "GeometryCollection/GeometryCollectionExampleFields.h"
#include "GeometryCollection/GeometryCollectionExampleMatrices.h"
#include "GeometryCollection/GeometryCollectionExampleSimulation.h"
#include "GeometryCollection/GeometryCollectionExampleSimulationField.h"
#include "GeometryCollection/GeometryCollectionExampleSimulationSolver.h"
#include "GeometryCollection/GeometryCollectionExampleSimulationStreaming.h"
#include "GeometryCollection/GeometryCollectionExampleProximity.h"
#include "GeometryCollection/GeometryCollectionExampleClean.h"
#include "GeometryCollection/GeometryCollectionExampleSpatialHash.h"

namespace GeometryCollectionExample
{
#define RUN_EXAMPLE(X) X<float>(RESPONSE());

#if TODO_REIMPLEMENT_RIGID_CLUSTERING
	template<class RESPONSE>
	void ExecuteExamples()
	{
		RUN_EXAMPLE(BasicGlobalMatrices);
		RUN_EXAMPLE(TransformMatrixElement);
		RUN_EXAMPLE(ReparentingMatrices);
		RUN_EXAMPLE(ParentTransformTest);
		RUN_EXAMPLE(ReindexMaterialsTest);
		RUN_EXAMPLE(AttributeTransferTest);
		RUN_EXAMPLE(AttributeDependencyTest);
		RUN_EXAMPLE(Creation);
		RUN_EXAMPLE(ContiguousElementsTest);
		RUN_EXAMPLE(DeleteFromEnd);
		RUN_EXAMPLE(DeleteFromStart);
		RUN_EXAMPLE(DeleteFromMiddle);
		RUN_EXAMPLE(DeleteBranch);
		RUN_EXAMPLE(DeleteRootLeafMiddle);
		RUN_EXAMPLE(DeleteEverything);
		RUN_EXAMPLE(Fields_NoiseSample);
		RUN_EXAMPLE(Fields_RadialIntMask);
		RUN_EXAMPLE(Fields_RadialFalloff);
		RUN_EXAMPLE(Fields_PlaneFalloff);
		RUN_EXAMPLE(Fields_UniformVector);
		RUN_EXAMPLE(Fields_RaidalVector);
		RUN_EXAMPLE(Fields_SumVectorFullMult);
		RUN_EXAMPLE(Fields_SumVectorFullDiv);
		RUN_EXAMPLE(Fields_SumVectorFullAdd);
		RUN_EXAMPLE(Fields_SumVectorFullSub);
		RUN_EXAMPLE(Fields_SumVectorLeftSide);
		RUN_EXAMPLE(Fields_SumVectorRightSide);
		RUN_EXAMPLE(Fields_SumScalar);
		RUN_EXAMPLE(Fields_SumScalarRightSide);
		RUN_EXAMPLE(Fields_SumScalarLeftSide);
		RUN_EXAMPLE(Fields_Culling);
		RUN_EXAMPLE(Fields_SerializeAPI);
		RUN_EXAMPLE(Solver_AdvanceNoObjects);
		RUN_EXAMPLE(Solver_AdvanceDisabledObjects);
		RUN_EXAMPLE(Solver_AdvanceDisabledClusteredObjects);
		RUN_EXAMPLE(Solver_ValidateReverseMapping);
		RUN_EXAMPLE(RigidBodiesFallingUnderGravity);
		RUN_EXAMPLE(RigidBodiesCollidingWithSolverFloor);
		RUN_EXAMPLE(RigidBodiesSingleSphereIntersectingWithSolverFloor);
		RUN_EXAMPLE(RigidBodiesSingleSphereCollidingWithSolverFloor);
		RUN_EXAMPLE(RigidBodiesKinematic);
		RUN_EXAMPLE(RigidBodiesSleepingActivation);
		RUN_EXAMPLE(RigidBodies_CollisionGroup);
		RUN_EXAMPLE(RigidBodies_ClusterTest_SingleLevelNonBreaking);
		RUN_EXAMPLE(RigidBodies_ClusterTest_DeactivateClusterParticle);
		RUN_EXAMPLE(RigidBodies_ClusterTest_SingleLevelBreaking);
		RUN_EXAMPLE(RigidBodies_ClusterTest_NestedCluster);
		RUN_EXAMPLE(RigidBodies_ClusterTest_NestedCluster_MultiStrain);
		RUN_EXAMPLE(RigidBodies_ClusterTest_NestedCluster_Halt);
		RUN_EXAMPLE(RigidBodies_ClusterTest_KinematicAnchor);
		RUN_EXAMPLE(RigidBodies_ClusterTest_StaticAnchor);
		RUN_EXAMPLE(RigidBodies_ClusterTest_UnionClusters);
		RUN_EXAMPLE(RigidBodies_ClusterTest_ReleaseClusterParticle_ClusteredNode);
		RUN_EXAMPLE(RigidBodies_ClusterTest_ReleaseClusterParticle_ClusteredKinematicNode);
		RUN_EXAMPLE(RigidBodies_ClusterTest_ReleaseClusterParticles_AllLeafNodes);
		RUN_EXAMPLE(RigidBodies_ClusterTest_ReleaseClusterParticles_ClusterNodeAndSubClusterNode);
		RUN_EXAMPLE(RigidBodies_Field_KinematicActivation);
		RUN_EXAMPLE(RigidBodies_Field_InitialLinearVelocity);
		RUN_EXAMPLE(RigidBodies_Field_StayDynamic);
		RUN_EXAMPLE(RigidBodies_Field_LinearForce);
		RUN_EXAMPLE(RigidBodies_Field_Torque);
		RUN_EXAMPLE(RigidBodies_Field_Kill);
		RUN_EXAMPLE(RigidBodies_Field_LinearVelocity);
		RUN_EXAMPLE(RigidBodies_Field_CollisionGroup);
		RUN_EXAMPLE(RigidBodies_Field_ClusterBreak_StrainModel_Test1);
		RUN_EXAMPLE(RigidBodies_Field_ClusterBreak_StrainModel_Test2);
		RUN_EXAMPLE(RigidBodies_Field_ClusterBreak_StrainModel_Test3);
		RUN_EXAMPLE(RigidBodies_Streaming_StartSolverEmpty);
		RUN_EXAMPLE(RigidBodies_Streaming_BulkInitialization);
		RUN_EXAMPLE(RigidBodies_Streaming_DeferedClusteringInitialization);
		RUN_EXAMPLE(BuildProximity);
		RUN_EXAMPLE(GeometryDeleteFromStart);
		RUN_EXAMPLE(GeometryDeleteFromEnd);
		RUN_EXAMPLE(GeometryDeleteFromMiddle);
		RUN_EXAMPLE(GeometryDeleteMultipleFromMiddle);
		RUN_EXAMPLE(GeometryDeleteRandom);
		RUN_EXAMPLE(GeometryDeleteRandom2);
		RUN_EXAMPLE(GeometryDeleteAll);
		RUN_EXAMPLE(GeometrySwapFlat);
		RUN_EXAMPLE(TestFracturedGeometry);
		RUN_EXAMPLE(TestDeleteCoincidentVertices);
		RUN_EXAMPLE(TestDeleteCoincidentVertices2);
		RUN_EXAMPLE(TestDeleteZeroAreaFaces);
		RUN_EXAMPLE(TestDeleteHiddenFaces);
		RUN_EXAMPLE(GetClosestPointsTest1);
		RUN_EXAMPLE(GetClosestPointsTest2);
		RUN_EXAMPLE(GetClosestPointsTest3);
		RUN_EXAMPLE(GetClosestPointTest);
		RUN_EXAMPLE(HashTableUpdateTest);
		RUN_EXAMPLE(HashTablePressureTest);
		RUN_EXAMPLE(TestGeometryDecimation);
		
	}
	template void ExecuteExamples<ExampleResponse>();
#endif
}
