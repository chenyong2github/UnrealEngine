// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaosApp.h"

#include "HeadlessChaos.h"
#include "HeadlessChaosTestBP.h"
#include "HeadlessChaosTestBroadphase.h"
#include "HeadlessChaosTestCloth.h"
#include "HeadlessChaosTestClustering.h"
#include "HeadlessChaosTestCollisions.h"
#include "HeadlessChaosTestForces.h"
#include "HeadlessChaosTestGJK.h"
#include "HeadlessChaosTestImplicits.h"
#include "HeadlessChaosTestRaycast.h"
#include "HeadlessChaosTestSerialization.h"
#include "HeadlessChaosTestSpatialHashing.h"
#include "Modules/ModuleManager.h"
#include "RequiredProgramMainCPPInclude.h"
#include "Chaos/PBDRigidsEvolution.h"
#include "Chaos/PBDJointConstraints.h"
#include "Chaos/Box.h"
#include "Chaos/Sphere.h"
#include "Chaos/Utilities.h"
#include "HeadlessChaosTestParticleHandle.h"
#include "HeadlessChaosTestClustering.h"
#include "HeadlessChaosTestSerialization.h"
#include "HeadlessChaosTestBP.h"
#include "HeadlessChaosTestRaycast.h"
#include "HeadlessChaosTestSweep.h"
#include "HeadlessChaosTestGJK.h"
#include "HeadlessChaosTestEPA.h"
#include "HeadlessChaosTestBroadphase.h"
#include "HeadlessChaosTestMostOpposing.h"
#include "HeadlessChaosTestSolverCommandList.h"
#include "HeadlessChaosTestSolverProxies.h"
#include "HeadlessChaosTestHandles.h"


#include "GeometryCollection/GeometryCollectionTest.h"
#include "GeometryCollection/GeometryCollectionTestBoneHierarchy.h"
#include "GeometryCollection/GeometryCollectionTestClean.h"
#include "GeometryCollection/GeometryCollectionTestClustering.h"
#include "GeometryCollection/GeometryCollectionTestCollisionResolution.h"
#include "GeometryCollection/GeometryCollectionTestCreation.h"
#include "GeometryCollection/GeometryCollectionTestDecimation.h"
#include "GeometryCollection/GeometryCollectionTestFields.h"
#include "GeometryCollection/GeometryCollectionTestImplicitCapsule.h"
#include "GeometryCollection/GeometryCollectionTestImplicitCylinder.h"
#include "GeometryCollection/GeometryCollectionTestImplicitSphere.h"
#include "GeometryCollection/GeometryCollectionTestInitilization.h"
#include "GeometryCollection/GeometryCollectionTestMassProperties.h"
#include "GeometryCollection/GeometryCollectionTestMatrices.h"
#include "GeometryCollection/GeometryCollectionTestProximity.h"
#include "GeometryCollection/GeometryCollectionTestResponse.h"
#include "GeometryCollection/GeometryCollectionTestSimulation.h"
#include "GeometryCollection/GeometryCollectionTestSimulationField.h"
#include "GeometryCollection/GeometryCollectionTestSimulationSolver.h"
#include "GeometryCollection/GeometryCollectionTestSimulationStreaming.h"
#include "GeometryCollection/GeometryCollectionTestSpatialHash.h"
#include "GeometryCollection/GeometryCollectionTestVisibility.h"
#include "GeometryCollection/GeometryCollectionTestEvents.h"
#include "GeometryCollection/GeometryCollectionTestSkeletalMeshPhysicsProxy.h"
#include "GeometryCollection/GeometryCollectionTestSerialization.h"


IMPLEMENT_APPLICATION(HeadlessChaos, "HeadlessChaos");

#define LOCTEXT_NAMESPACE "HeadlessChaos"

DEFINE_LOG_CATEGORY(LogHeadlessChaos);

TEST(ImplicitTests, Implicit) {
	ChaosTest::ImplicitPlane<float>();
	ChaosTest::ImplicitCube<float>();
	ChaosTest::ImplicitSphere<float>();
	ChaosTest::ImplicitCylinder<float>();
	ChaosTest::ImplicitTaperedCylinder<float>();
	ChaosTest::ImplicitCapsule<float>();
	ChaosTest::ImplicitScaled<float>();
	ChaosTest::ImplicitScaled2<float>();
	ChaosTest::ImplicitTransformed<float>();
	ChaosTest::ImplicitIntersection<float>();
	ChaosTest::ImplicitUnion<float>();
	ChaosTest::UpdateImplicitUnion<float>();
	// @todo: Make this work at some point
	//ChaosTest::ImplicitLevelset<float>();

	SUCCEED();
}

TEST(ImplicitTests, Rasterization) {
	ChaosTest::RasterizationImplicit<float>();
	ChaosTest::RasterizationImplicitWithHole<float>();
}

TEST(ImplicitTests, ConvexHull) {
	ChaosTest::ConvexHull<float>();
	ChaosTest::ConvexHull2<float>();
	ChaosTest::Simplify<float>();
}

TEST(CollisionTests, Collisions) {
	GEnsureOnNANDiagnostic = 1;

	ChaosTest::LevelsetConstraint<float>();
	// ChaosTest::LevelsetConstraintGJK<float>();
	ChaosTest::CollisionBoxPlane<float>();
	ChaosTest::CollisionBoxPlaneZeroResitution<float>();
	ChaosTest::CollisionBoxPlaneRestitution<float>();
	ChaosTest::CollisionCubeCubeRestitution<float>();
	ChaosTest::CollisionBoxToStaticBox<float>();
	ChaosTest::CollisionConvexConvex<float>();

	// @ todo: Make this work at some point
	//ChaosTest::SpatialHashing<float>();

	SUCCEED();
}

TEST(CollisionTests, PGS) {
	ChaosTest::CollisionPGS<float>();
	ChaosTest::CollisionPGS2<float>();
	SUCCEED();
}

TEST(Clustering, Clustering) {
	ChaosTest::ImplicitCluster<float>();
	ChaosTest::FractureCluster<float>();
	ChaosTest::PartialFractureCluster<float>();
	SUCCEED();
}

TEST(SerializationTests, Serialization) {
	ChaosTest::SimpleObjectsSerialization<float>();
	ChaosTest::SharedObjectsSerialization<float>();
	ChaosTest::GraphSerialization<float>();
	ChaosTest::ObjectUnionSerialization<float>();
	ChaosTest::ParticleSerialization<float>();
	ChaosTest::BVHSerialization<float>();
	ChaosTest::RigidParticlesSerialization<float>();
	ChaosTest::BVHParticlesSerialization<float>();
	SUCCEED();
}

TEST(BroadphaseTests, Broadphase) {
	ChaosTest::BPPerfTest<float>();
	//ChaosTest::SpatialAccelerationDirtyAndGlobalQueryStrestTest<float>();
	SUCCEED();
}

//TEST(ClothTests, DeformableGravity) {
//	ChaosTest::DeformableGravity<float>();
//
//	SUCCEED();
//}
//
//TEST(ClothTests, EdgeConstraints) {
//	ChaosTest::EdgeConstraints<float>();
//
//	SUCCEED();
//}

TEST(RaycastTests, Raycast) {
	ChaosTest::SphereRaycast<float>();
	ChaosTest::PlaneRaycast<float>();
	//ChaosTest::CylinderRaycast<float>();
	//ChaosTest::TaperedCylinderRaycast<float>();
	ChaosTest::CapsuleRaycast<float>();
	ChaosTest::TriangleRaycast<float>();
	ChaosTest::BoxRaycast<float>();
	ChaosTest::ScaledRaycast<float>();
	//ChaosTest::TransformedRaycast<float>();
	//ChaosTest::UnionRaycast<float>();
	//ChaosTest::IntersectionRaycast<float>();
	
	SUCCEED();
}

TEST(SweepTests, Sweep) {
	ChaosTest::CapsuleSweepAgainstTriMeshReal<float>();
}

TEST(MostOpposingTests, MostOpposing) {
	ChaosTest::TrimeshMostOpposing<float>();
	ChaosTest::ConvexMostOpposing<float>();
	ChaosTest::ScaledMostOpposing<float>();

	SUCCEED();
}

TEST(GJK, Simplexes) {
	ChaosTest::SimplexLine<float>();
	ChaosTest::SimplexTriangle<float>();
	ChaosTest::SimplexTetrahedron<float>();
	
	SUCCEED();
}

TEST(GJK, GJKIntersectTests) {
	ChaosTest::GJKSphereSphereTest<float>();
	ChaosTest::GJKSphereBoxTest<float>();
	ChaosTest::GJKSphereCapsuleTest<float>();
	ChaosTest::GJKSphereConvexTest<float>();
	ChaosTest::GJKSphereScaledSphereTest<float>();
	
	SUCCEED();
}

TEST(GJK, GJKRaycastTests) {
	ChaosTest::GJKSphereSphereSweep<float>();
	ChaosTest::GJKSphereBoxSweep<float>();
	ChaosTest::GJKSphereCapsuleSweep<float>();
	ChaosTest::GJKSphereConvexSweep<float>();
	ChaosTest::GJKSphereScaledSphereSweep<float>();
	ChaosTest::GJKBoxCapsuleSweep<float>();
	ChaosTest::GJKBoxBoxSweep<float>();
	ChaosTest::GJKCapsuleConvexInitialOverlapSweep<float>();
	SUCCEED();
}

TEST(EPA, EPATests) {
	ChaosTest::EPAInitTest<float>();
	ChaosTest::EPASimpleTest<float>();
	SUCCEED();
}

TEST(BP, BroadphaseTests) {
	ChaosTest::GridBPTest<float>();
	ChaosTest::GridBPTest2<float>();
	ChaosTest::AABBTreeTest<float>();
	ChaosTest::AABBTreeTimesliceTest<float>();
	ChaosTest::BroadphaseCollectionTest<float>();
	SUCCEED();
}

TEST(ParticleHandle, ParticleHandleTests)
{
	ChaosTest::ParticleIteratorTest<float>();
	ChaosTest::ParticleHandleTest<float>();
	ChaosTest::AccelerationStructureHandleComparison();
	ChaosTest::HandleObjectStateChangeTest();
	SUCCEED();
}

TEST(Perf, PerfTests)
{
	ChaosTest::EvolutionPerfHarness();
	SUCCEED();
}

TEST(Handles, FrameworkTests)
{
	ChaosTest::Handles::HandleArrayTest<float>();
	ChaosTest::Handles::HandleHeapTest<float>();
	ChaosTest::Handles::HandleSerializeTest<float>();
}

//TEST(Vehicle, VehicleTests) {
//
//	ChaosTest::SystemTemplateTest<float>();
//
//	ChaosTest::AerofoilTestLiftDrag<float>();
//	
//	ChaosTest::TransmissionTestManualGearSelection<float>();
//	ChaosTest::TransmissionTestAutoGearSelection<float>();
//	ChaosTest::TransmissionTestGearRatios<float>();
//
//	ChaosTest::EngineRPM<float>();
//
//	ChaosTest::WheelLateralSlip<float>();
//	ChaosTest::WheelBrakingLongitudinalSlip<float>();
//	ChaosTest::WheelAcceleratingLongitudinalSlip<float>();
//
//	ChaosTest::SuspensionForce<float>();
//}


//////////////////////////////////////////////////////////
///// GEOMETRY COLLECTION ///////////////////////////////


// Matrices Tests
TEST(GeometryCollection_MatricesTest,BasicGlobalMatrices) { GeometryCollectionTest::BasicGlobalMatrices<float>();SUCCEED(); }
TEST(GeometryCollection_MatricesTest,TransformMatrixElement) { GeometryCollectionTest::TransformMatrixElement<float>(); SUCCEED(); }
TEST(GeometryCollection_MatricesTest,ReparentingMatrices) { GeometryCollectionTest::ReparentingMatrices<float>(); SUCCEED(); }

// Creation Tests
TEST(GeometryCollection_CreationTest,CheckIncrementMask) { GeometryCollectionTest::CheckIncrementMask<float>(); SUCCEED(); }
TEST(GeometryCollection_CreationTest,Creation) { GeometryCollectionTest::Creation<float>(); SUCCEED(); }
TEST(GeometryCollection_CreationTest,Empty) { GeometryCollectionTest::Empty<float>(); SUCCEED(); }
TEST(GeometryCollection_CreationTest,AppendTransformHierarchy) { GeometryCollectionTest::AppendTransformHierarchy<float>(); SUCCEED(); }
TEST(GeometryCollection_CreationTest,ParentTransformTest) { GeometryCollectionTest::ParentTransformTest<float>(); SUCCEED(); }
TEST(GeometryCollection_CreationTest,DeleteFromEnd) { GeometryCollectionTest::DeleteFromEnd<float>(); SUCCEED(); }
TEST(GeometryCollection_CreationTest,DeleteFromStart) { GeometryCollectionTest::DeleteFromStart<float>(); SUCCEED(); }
TEST(GeometryCollection_CreationTest,DeleteFromMiddle) { GeometryCollectionTest::DeleteFromMiddle<float>(); SUCCEED(); }
TEST(GeometryCollection_CreationTest,DeleteBranch) { GeometryCollectionTest::DeleteBranch<float>(); SUCCEED(); }
TEST(GeometryCollection_CreationTest,DeleteRootLeafMiddle) { GeometryCollectionTest::DeleteRootLeafMiddle<float>(); SUCCEED(); }
TEST(GeometryCollection_CreationTest,DeleteEverything) { GeometryCollectionTest::DeleteEverything<float>(); SUCCEED(); }
TEST(GeometryCollection_CreationTest,ReindexMaterialsTest) { GeometryCollectionTest::ReindexMaterialsTest<float>(); SUCCEED(); }
TEST(GeometryCollection_CreationTest,ContiguousElementsTest) { GeometryCollectionTest::ContiguousElementsTest<float>(); SUCCEED(); }
TEST(GeometryCollection_CreationTest,AttributeDependencyTest) { GeometryCollectionTest::AttributeDependencyTest<float>(); SUCCEED(); }


// Proximity Tests
TEST(GeometryCollection_ProximityTest,BuildProximity) { GeometryCollectionTest::BuildProximity<float>(); SUCCEED(); }
TEST(GeometryCollection_ProximityTest,GeometryDeleteFromStart) { GeometryCollectionTest::GeometryDeleteFromStart<float>(); SUCCEED(); }
TEST(GeometryCollection_ProximityTest,GeometryDeleteFromEnd) { GeometryCollectionTest::GeometryDeleteFromEnd<float>(); SUCCEED(); }
TEST(GeometryCollection_ProximityTest,GeometryDeleteFromMiddle) { GeometryCollectionTest::GeometryDeleteFromMiddle<float>(); SUCCEED(); }
TEST(GeometryCollection_ProximityTest,GeometryDeleteMultipleFromMiddle) { GeometryCollectionTest::GeometryDeleteMultipleFromMiddle<float>(); SUCCEED(); }
TEST(GeometryCollection_ProximityTest,GeometryDeleteRandom) { GeometryCollectionTest::GeometryDeleteRandom<float>(); SUCCEED(); }
TEST(GeometryCollection_ProximityTest,GeometryDeleteRandom2) { GeometryCollectionTest::GeometryDeleteRandom2<float>(); SUCCEED(); }
TEST(GeometryCollection_ProximityTest,GeometryDeleteAll) { GeometryCollectionTest::GeometryDeleteAll<float>(); SUCCEED(); }
TEST(GeometryCollection_ProximityTest,GeometrySwapFlat) { GeometryCollectionTest::GeometrySwapFlat<float>(); SUCCEED(); }
TEST(GeometryCollection_ProximityTest,TestFracturedGeometry) { GeometryCollectionTest::TestFracturedGeometry<float>(); SUCCEED(); }

// Clean Tests
TEST(GeometryCollection_CleanTest,TestDeleteCoincidentVertices) { GeometryCollectionTest::TestDeleteCoincidentVertices<float>(); SUCCEED(); }
TEST(GeometryCollection_CleanTest,TestDeleteCoincidentVertices2) { GeometryCollectionTest::TestDeleteCoincidentVertices2<float>(); SUCCEED(); }
TEST(GeometryCollection_CleanTest,TestDeleteZeroAreaFaces) { GeometryCollectionTest::TestDeleteZeroAreaFaces<float>(); SUCCEED(); }
TEST(GeometryCollection_CleanTest,TestDeleteHiddenFaces) { GeometryCollectionTest::TestDeleteHiddenFaces<float>(); SUCCEED(); }
TEST(GeometryCollection_CleanTest,TestFillHoles) { GeometryCollectionTest::TestFillHoles<float>(); SUCCEED(); }

// SpatialHash Tests
TEST(GeometryCollection_SpatialHashTest,GetClosestPointsTest1) { GeometryCollectionTest::GetClosestPointsTest1<float>(); SUCCEED(); }
TEST(GeometryCollection_SpatialHashTest,GetClosestPointsTest2) { GeometryCollectionTest::GetClosestPointsTest2<float>(); SUCCEED(); }
TEST(GeometryCollection_SpatialHashTest,GetClosestPointsTest3) { GeometryCollectionTest::GetClosestPointsTest3<float>(); SUCCEED(); }
TEST(GeometryCollection_SpatialHashTest,GetClosestPointTest) { GeometryCollectionTest::GetClosestPointTest<float>(); SUCCEED(); }
TEST(GeometryCollection_SpatialHashTest,HashTableUpdateTest) { GeometryCollectionTest::HashTableUpdateTest<float>(); SUCCEED(); }
TEST(GeometryCollection_SpatialHashTest,HashTablePressureTest) { GeometryCollectionTest::HashTablePressureTest<float>(); SUCCEED(); }

// HideVertices Test
TEST(GeometryCollection_HideVerticesTest,TestHideVertices) { GeometryCollectionTest::TestHideVertices<float>(); SUCCEED(); }

// Object Collision Test
//TEST(GeometryCollection_CollisionTest, DISABLED_TestGeometryDecimation) { GeometryCollectionTest::TestGeometryDecimation<float>(); SUCCEED(); }  Fix or remove support for decimation
TEST(GeometryCollection_CollisionTest,TestImplicitCapsule) { GeometryCollectionTest::TestImplicitCapsule<float>(); SUCCEED(); }
TEST(GeometryCollection_CollisionTest,TestImplicitCylinder) { GeometryCollectionTest::TestImplicitCylinder<float>(); SUCCEED(); }
TEST(GeometryCollection_CollisionTest,TestImplicitSphere) { GeometryCollectionTest::TestImplicitSphere<float>(); SUCCEED(); }
TEST(GeometryCollection_CollisionTest,TestImplicitBoneHierarchy) { GeometryCollectionTest::TestImplicitBoneHierarchy<float>(); SUCCEED(); }

// Fields Tests
TEST(GeometryCollection_FieldTest,Fields_NoiseSample) { GeometryCollectionTest::Fields_NoiseSample(); SUCCEED(); }
TEST(GeometryCollection_FieldTest,Fields_RadialIntMask) { GeometryCollectionTest::Fields_RadialIntMask(); SUCCEED(); }
TEST(GeometryCollection_FieldTest,Fields_RadialFalloff) { GeometryCollectionTest::Fields_RadialFalloff(); SUCCEED(); }
TEST(GeometryCollection_FieldTest,Fields_PlaneFalloff) { GeometryCollectionTest::Fields_PlaneFalloff(); SUCCEED(); }
TEST(GeometryCollection_FieldTest,Fields_UniformVector) { GeometryCollectionTest::Fields_UniformVector(); SUCCEED(); }
TEST(GeometryCollection_FieldTest,Fields_RaidalVector) { GeometryCollectionTest::Fields_RaidalVector(); SUCCEED(); }
TEST(GeometryCollection_FieldTest,Fields_SumVectorFullMult) { GeometryCollectionTest::Fields_SumVectorFullMult(); SUCCEED(); }
TEST(GeometryCollection_FieldTest,Fields_SumVectorFullDiv) { GeometryCollectionTest::Fields_SumVectorFullDiv(); SUCCEED(); }
TEST(GeometryCollection_FieldTest,Fields_SumVectorFullAdd) { GeometryCollectionTest::Fields_SumVectorFullAdd(); SUCCEED(); }
TEST(GeometryCollection_FieldTest,Fields_SumVectorFullSub) { GeometryCollectionTest::Fields_SumVectorFullSub(); SUCCEED(); }
TEST(GeometryCollection_FieldTest,Fields_SumVectorLeftSide) { GeometryCollectionTest::Fields_SumVectorLeftSide(); SUCCEED(); }
TEST(GeometryCollection_FieldTest,Fields_SumVectorRightSide) { GeometryCollectionTest::Fields_SumVectorRightSide(); SUCCEED(); }
TEST(GeometryCollection_FieldTest,Fields_SumScalar) { GeometryCollectionTest::Fields_SumScalar(); SUCCEED(); }
TEST(GeometryCollection_FieldTest,Fields_SumScalarRightSide) { GeometryCollectionTest::Fields_SumScalarRightSide(); SUCCEED(); }
TEST(GeometryCollection_FieldTest,Fields_SumScalarLeftSide) { GeometryCollectionTest::Fields_SumScalarLeftSide(); SUCCEED(); }
TEST(GeometryCollection_FieldTest,Fields_Culling) { GeometryCollectionTest::Fields_Culling(); SUCCEED(); }
TEST(GeometryCollection_FieldTest,Fields_SerializeAPI) { GeometryCollectionTest::Fields_SerializeAPI(); SUCCEED(); }

//TEST(GeometryCollectionTest,RigidBodies_CollisionGroup); // fix me
//
// Broken
//

/*
TEST(GeometryCollectionTest, RigidBodies_ClusterTest_KinematicAnchor) { GeometryCollectionTest::RigidBodies_ClusterTest_KinematicAnchor<float>(); SUCCEED(); }
TEST(GeometryCollectionTest, RigidBodies_ClusterTest_StaticAnchor) { GeometryCollectionTest::RigidBodies_ClusterTest_StaticAnchor<float>(); SUCCEED(); }
TEST(GeometryCollectionTest, RigidBodies_ClusterTest_ReleaseClusterParticles_AllLeafNodes) { GeometryCollectionTest::RigidBodies_ClusterTest_ReleaseClusterParticles_AllLeafNodes<float>(); SUCCEED(); }
TEST(GeometryCollectionTest, RigidBodies_ClusterTest_ReleaseClusterParticles_ClusterNodeAndSubClusterNode) { GeometryCollectionTest::RigidBodies_ClusterTest_ReleaseClusterParticles_ClusterNodeAndSubClusterNode<float>(); SUCCEED(); }
TEST(GeometryCollectionTest, RigidBodies_ClusterTest_RemoveOnFracture) { GeometryCollectionTest::RigidBodies_ClusterTest_RemoveOnFracture<float>(); SUCCEED(); }
TEST(GeometryCollectionTest, RigidBodiess_ClusterTest_ParticleImplicitCollisionGeometry) { GeometryCollectionTest::RigidBodiess_ClusterTest_ParticleImplicitCollisionGeometry<float>(); SUCCEED(); }
*/


// SimulationStreaming Tests
// broken and/or crashing
/*
TEST(GeometryCollectionTest, RigidBodies_Streaming_StartSolverEmpty) { GeometryCollectionTest::RigidBodies_Streaming_StartSolverEmpty<float>(); SUCCEED(); }
TEST(GeometryCollectionTest, RigidBodies_Streaming_BulkInitialization) { GeometryCollectionTest::RigidBodies_Streaming_BulkInitialization<float>(); SUCCEED(); }
TEST(GeometryCollectionTest, RigidBodies_Streaming_DeferedClusteringInitialization) { GeometryCollectionTest::RigidBodies_Streaming_DeferedClusteringInitialization<float>(); SUCCEED(); }
*/


// Secondary Particle Events
//TEST(GeometryCollectionTest, Solver_ValidateReverseMapping) { GeometryCollectionTest::Solver_ValidateReverseMapping<float>(); SUCCEED(); }



// Static and Skeletal Mesh Tests
// broken and/or crashing
/*
TEST(SkeletalMeshPhysicsProxyTest, RegistersCorrectly){GeometryCollectionTest::TestSkeletalMeshPhysicsProxy_Register<float>();SUCCEED();}
TEST(SkeletalMeshPhysicsProxyTest, KinematicBonesMoveCorrectly){GeometryCollectionTest::TestSkeletalMeshPhysicsProxy_Kinematic<float>();SUCCEED();}
TEST(SkeletalMeshPhysicsProxyTest, DynamicBonesMoveCorrectly){GeometryCollectionTest::TestSkeletalMeshPhysicsProxy_Dynamic<float>();SUCCEED();}
*/

// Serialization
TEST(GeometryCollectionSerializationTests,GeometryCollectionSerializesCorrectly){ GeometryCollectionTests::GeometryCollectionSerialization<float>();SUCCEED(); }


/**/

class UEGTestPrinter : public ::testing::EmptyTestEventListener
{
    virtual void OnTestStart(const ::testing::TestInfo& TestInfo)
    {
        UE_LOG(LogHeadlessChaos, Verbose, TEXT("Test %s.%s Starting"), *FString(TestInfo.test_case_name()), *FString(TestInfo.name()));
    }

    virtual void OnTestPartResult(const ::testing::TestPartResult& TestPartResult)
    {
        if (TestPartResult.failed())
        {
            UE_LOG(LogHeadlessChaos, Error, TEXT("FAILED in %s:%d\n%s"), *FString(TestPartResult.file_name()), TestPartResult.line_number(), *FString(TestPartResult.summary()))
        }
        else
        {
            UE_LOG(LogHeadlessChaos, Verbose, TEXT("Succeeded in %s:%d\n%s"), *FString(TestPartResult.file_name()), TestPartResult.line_number(), *FString(TestPartResult.summary()))
        }
    }

    virtual void OnTestEnd(const ::testing::TestInfo& TestInfo)
    {
        UE_LOG(LogHeadlessChaos, Verbose, TEXT("Test %s.%s Ending"), *FString(TestInfo.test_case_name()), *FString(TestInfo.name()));
    }
};

INT32_MAIN_INT32_ARGC_TCHAR_ARGV()
{
    // start up the main loop
	GEngineLoop.PreInit(ArgC, ArgV);
	FModuleManager::Get().StartProcessingNewlyLoadedObjects();
	
	::testing::InitGoogleTest(&ArgC, ArgV);

	// Add a UE-formatting printer
    ::testing::TestEventListeners& Listeners = ::testing::UnitTest::GetInstance()->listeners();
    Listeners.Append(new UEGTestPrinter);

	ensure(RUN_ALL_TESTS() == 0);

	FCoreDelegates::OnExit.Broadcast();
	FModuleManager::Get().UnloadModulesAtShutdown();

	FPlatformMisc::RequestExit(false);

	return 0;
}

#undef LOCTEXT_NAMESPACE
