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
#include "HeadlessChaosTestGJK.h"
#include "HeadlessChaosTestEPA.h"
#include "HeadlessChaosTestBroadphase.h"
#include "HeadlessChaosTestMostOpposing.h"
#include "HeadlessChaosTestSolverCommandList.h"
#include "HeadlessChaosTestSolverProxies.h"
#include "HeadlessChaosTestHandles.h"


IMPLEMENT_APPLICATION(HeadlessChaos, "HeadlessChaos");

#define LOCTEXT_NAMESPACE "HeadlessChaos"

DEFINE_LOG_CATEGORY(LogHeadlessChaos);

TEST(ImplicitTests, Implicit) {
	ChaosTest::ImplicitPlane<float>();
	//ChaosTest::ImplicitCube<float>();
	ChaosTest::ImplicitSphere<float>();
	ChaosTest::ImplicitCylinder<float>();
	ChaosTest::ImplicitTaperedCylinder<float>();
	ChaosTest::ImplicitCapsule<float>();
	ChaosTest::ImplicitScaled<float>();
	ChaosTest::ImplicitScaled2<float>();
	ChaosTest::ImplicitTransformed<float>();
	ChaosTest::ImplicitIntersection<float>();
	ChaosTest::ImplicitUnion<float>();
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

TEST(ForceTests, Forces) {
	ChaosTest::Gravity<Chaos::TPBDRigidsEvolutionGBF<float, 3>, float>();
#if CHAOS_PARTICLEHANDLE_TODO
	ChaosTest::Gravity<Chaos::TPBDRigidsEvolutionPGS<float, 3>, float>();
#endif
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
	ChaosTest::TestPendingSpatialDataHandlePointerConflict();
	SUCCEED();
}

TEST(ClothTests, DeformableGravity) {
	ChaosTest::DeformableGravity<float>();

	SUCCEED();
}

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


TEST(ChaosSolver, ChaosSolverTests)
{
	ChaosTest::SingleParticleProxySingleThreadTest<float>();
	ChaosTest::CommandListTest<float>();
}
TEST(ChaosSolver, DISABLED_ChaosSolverTests)
{
	ChaosTest::SingleParticleProxyTaskGraphTest<float>();
}

TEST(Handles, FrameworkTests)
{
	ChaosTest::Handles::HandleArrayTest<float>();
	ChaosTest::Handles::HandleHeapTest<float>();
	ChaosTest::Handles::HandleSerializeTest<float>();
}


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
