// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaos.h"
#include "HeadlessChaosTestConstraints.h"

#include "Modules/ModuleManager.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDConstraintRule.h"
#include "Chaos/PBDRigidsEvolution.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/PBDJointConstraints.h"
#include "Chaos/PBDRigidDynamicSpringConstraints.h"
#include "Chaos/PBDRigidSpringConstraints.h"
#include "Chaos/Box.h"
#include "Chaos/Plane.h"
#include "Chaos/Sphere.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/ImplicitObjectTransformed.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/Levelset.h"
#include "Chaos/UniformGrid.h"
#include "Chaos/Utilities.h"

namespace ChaosTest {

	using namespace Chaos;


	/**
	 * Base class for joint constraint tests.
	 * Initialize the particle and joint data in the test code and call Create()
	 */
	class FJointConstraintsTest : public FConstraintsTest
	{
	public:
		FJointConstraintsTest(const int32 NumIterations, const FReal Gravity)
			: FConstraintsTest(NumIterations, Gravity)
			, JointsRule(Joints)
		{
			Evolution.AddConstraintRule(&JointsRule);
		}

		FPBDJointConstraintHandle* AddJoint(const TVector<TGeometryParticleHandle<FReal, 3>*, 2>& InConstrainedParticleIndices, const FVec3& InLocation)
		{
			return Joints.AddConstraint(InConstrainedParticleIndices, FRigidTransform3(InLocation, FRotation3::FromIdentity()));
		}

		virtual void Create()
		{
			for (int32 ParticleIndex = 0; ParticleIndex < ParticlePositions.Num(); ++ParticleIndex)
			{
				AddParticleBox(ParticlePositions[ParticleIndex], FRotation3::MakeFromEuler(FVec3(0.f, 0.f, 0.f)).GetNormalized(), ParticleSizes[ParticleIndex], ParticleMasses[ParticleIndex]);
			}

			for (int32 JointIndex = 0; JointIndex < JointPositions.Num(); ++JointIndex)
			{
				const TVector<TGeometryParticleHandle<FReal, 3>*, 2> ConstraintedParticleIds(GetParticle(JointParticleIndices[JointIndex][0]), GetParticle(JointParticleIndices[JointIndex][1]));
				AddJoint(ConstraintedParticleIds, JointPositions[JointIndex]);
			}
		}

		// Initial particles setup
		TArray<FVec3> ParticlePositions;
		TArray<FVec3> ParticleSizes;
		TArray<FReal> ParticleMasses;

		// Initial joints setup
		TArray<FVec3> JointPositions;
		TArray<TVector<int32, 2>> JointParticleIndices;

		// Solver state
		FPBDJointConstraints Joints;
		TPBDConstraintIslandRule<FPBDJointConstraints> JointsRule;
	};

	/**
	 * One kinematic, one dynamic particle connected by a ball-socket joint in the middle.
	 */
	void JointConstraint_Single()
	{

		const int32 NumIterations = 1;
		const FReal Gravity = 980;

		FJointConstraintsTest Test(NumIterations, Gravity);

		Test.ParticlePositions =
		{
			{ (FReal)0, (FReal)0, (FReal)1000 },
			{ (FReal)500, (FReal)0, (FReal)1000 },
		};
		Test.ParticleSizes =
		{
			{ (FReal)100, (FReal)100, (FReal)100 },
			{ (FReal)100, (FReal)100, (FReal)100 },
		};
		Test.ParticleMasses =
		{
			(FReal)0,
			(FReal)1,
		};

		Test.JointPositions =
		{
			{ (FReal)250, (FReal)0, (FReal)1000 },
		};
		Test.JointParticleIndices =
		{
			{ 0, 1 },
		};

		Test.Create();

		const int32 Box1Id = 0;
		const int32 Box2Id = 1;
		const float ExpectedDistance = (Test.ParticlePositions[1] - Test.ParticlePositions[0]).Size();
		const FVec3 Box2LocalSpaceJointPosition = Test.JointPositions[0] - Test.ParticlePositions[1];

		const FReal Dt = 0.01f;
		for (int32 i = 0; i < 100; ++i)
		{
			Test.Evolution.AdvanceOneTimeStep(Dt);
			Test.Evolution.EndFrame(Dt);


			// Joint position calculated from pose and local-space joint pos
			const FVec3 Box2WorldSpaceJointPosition = Test.GetParticle(Box2Id)->R().RotateVector(Box2LocalSpaceJointPosition) + Test.GetParticle(Box2Id)->X();
			EXPECT_LT((Box2WorldSpaceJointPosition - Test.JointPositions[0]).Size(), (FReal)0.1);

			// Kinematic particle should not have moved
			EXPECT_LT((Test.GetParticle(Box1Id)->X() - Test.ParticlePositions[0]).Size(), (FReal)0.1);
		}
	}

	void JointConstraint_SingleMoveRoot()
	{
		const int32 NumIterations = 5;
		const FReal Gravity = 0;
		const FReal BoxSize = 1;
		const FReal BoxMass = 1;
		const FReal Dt = (FReal)1 / 20;
		const FVec3 RootDelta(1 * BoxSize, 0, 0);

		FJointConstraintsTest Test(NumIterations, Gravity);

		Test.ParticlePositions =
		{
			{ (FReal)0, (FReal)0, (FReal)10 * BoxSize },
			{ (FReal)0, (FReal)0, (FReal)5 * BoxSize },
		};
		Test.ParticleSizes =
		{
			{ (FReal)BoxSize, (FReal)BoxSize, (FReal)BoxSize },
			{ (FReal)BoxSize, (FReal)BoxSize, (FReal)BoxSize },
		};
		Test.ParticleMasses =
		{
			(FReal)0,
			(FReal)BoxMass,
		};

		Test.JointPositions =
		{
			Test.ParticlePositions[0],
		};
		Test.JointParticleIndices =
		{
			{ 0, 1 },
		};

		Test.Create();

		const int32 Box1Id = 0;
		const int32 Box2Id = 1;
		const float ExpectedDistance = (Test.ParticlePositions[1] - Test.ParticlePositions[0]).Size();
		const FVec3 Box2LocalSpaceJointPosition = Test.JointPositions[0] - Test.ParticlePositions[1];

		// Everything should be in a stable state
		for (int32 i = 0; i < 10; ++i)
		{
			Test.Evolution.AdvanceOneTimeStep(Dt);
			Test.Evolution.EndFrame(Dt);

			// Nothing should have moved
			for (int32 ParticleIndex = 0; ParticleIndex < Test.ParticlePositions.Num(); ++ParticleIndex)
			{
				EXPECT_LT((Test.GetParticle(ParticleIndex)->X() - Test.ParticlePositions[ParticleIndex]).Size(), (FReal)0.1) << "Initial configuration instability on frame " << i;
			}
		}

		// Move the kinematic body
		const FVec3 RootPosition = Test.ParticlePositions[0] + RootDelta;
		Test.GetParticle(Box1Id)->X() = RootPosition;

		for (int32 i = 0; i < 1000; ++i)
		{
			Test.Evolution.AdvanceOneTimeStep(Dt);
			Test.Evolution.EndFrame(Dt);

			// Kinematic particle should have moved to animated position
			EXPECT_LT((Test.GetParticle(Box1Id)->X() - RootPosition).Size(), (FReal)0.01 * BoxSize) << "Post-move instability on frame " << i;

			// Particles should remain fixed distance apart (joint point is at Box1 location)
			const FVec3 Delta = Test.GetParticle(Box2Id)->CastToRigidParticle()->P() - Test.GetParticle(Box1Id)->X();
			const float Distance = Delta.Size();
			EXPECT_NEAR(Distance, ExpectedDistance, (FReal)0.01 * BoxSize) << "Post-move instability on frame " << i;

			// Joint position calculted from pose and local-space joint pos
			const FVec3 Box2WorldSpaceJointPosition = Test.GetParticle(Box2Id)->R().RotateVector(Box2LocalSpaceJointPosition) + Test.GetParticle(Box2Id)->X();
			EXPECT_LT((Box2WorldSpaceJointPosition - RootPosition).Size(), (FReal)0.01 * BoxSize) << "Post-move instability on frame " << i;
		}
	}


	/**
	 * Pendulum with animated root.
	 */
	void JointConstraint_SingleAnimated()
	{
		const int32 NumIterations = 5;
		const FReal Gravity = 980;
		const FReal BoxSize = 100;
		const FReal BoxMass = 1000;
		const FReal Dt = (FReal)1 / 20;
		const FReal AnimPeriod = (FReal)2;
		const FVec3 AnimDelta = FVec3(10 * BoxSize, 0, 0);

		FJointConstraintsTest Test(NumIterations, Gravity);

		Test.ParticlePositions =
		{
			{ (FReal)0, (FReal)0, (FReal)10 * BoxSize },
			{ (FReal)0, (FReal)2 * BoxSize, (FReal)10 * BoxSize },
		};
		Test.ParticleSizes =
		{
			{ (FReal)BoxSize, (FReal)BoxSize, (FReal)BoxSize },
			{ (FReal)BoxSize, (FReal)BoxSize, (FReal)BoxSize },
		};
		Test.ParticleMasses =
		{
			(FReal)0,
			(FReal)BoxMass,
		};

		Test.JointPositions =
		{
			Test.ParticlePositions[0],
		};
		Test.JointParticleIndices =
		{
			{ 0, 1 },
		};

		Test.Create();

		const int32 Box1Id = 0;
		const int32 Box2Id = 1;
		const float ExpectedDistance = (Test.ParticlePositions[1] - Test.ParticlePositions[0]).Size();
		const FVec3 Box2LocalSpaceJointPosition = Test.JointPositions[0] - Test.ParticlePositions[1];

		for (int32 i = 0; i < 1000; ++i)
		{
			const FReal Time = i * Dt;
			const FVec3 RootOffset = FMath::Sin((FReal)2 * PI * Time / AnimPeriod) * AnimDelta;
			const FVec3 RootPosition = Test.ParticlePositions[0] + RootOffset;

			Test.GetParticle(Box1Id)->X() = RootPosition;

			Test.Evolution.AdvanceOneTimeStep(Dt);
			Test.Evolution.EndFrame(Dt);

			// Kinematic particle should have moved to animated position
			EXPECT_LT((Test.GetParticle(Box1Id)->X() - RootPosition).Size(), (FReal)1) << "Failed on frame " << i;

			// Particles should remain fixed distance apart (joint point is at Box1 location)
			const FVec3 Delta = Test.GetParticle(Box2Id)->CastToRigidParticle()->P() - Test.GetParticle(Box1Id)->X();
			const float Distance = Delta.Size();
			EXPECT_NEAR(Distance, ExpectedDistance, (FReal)1) << "Failed on frame " << i;

			// Joint position calculated from pose and local-space joint pos
			const FVec3 Box2WorldSpaceJointPosition = Test.GetParticle(Box2Id)->R().RotateVector(Box2LocalSpaceJointPosition) + Test.GetParticle(Box2Id)->X();
			EXPECT_LT((Box2WorldSpaceJointPosition - RootPosition).Size(), (FReal)1) << "Failed on frame " << i;
		}
	}

	/**
	 * Pendulum with animated root.
	 */
	void JointConstraint_ShortChainAnimated()
	{
		const int32 NumIterations = 10;
		const FReal Gravity = 980;
		const FReal BoxSize = 100;
		const FReal BoxMass = 1000;
		const FReal Dt = (FReal)1 / 100;
		const FReal AnimPeriod = (FReal)1;
		const FVec3 AnimDelta = FVec3(5 * BoxSize, 0, 0);
		const FReal AcceptableDistanceError = 5;

		FJointConstraintsTest Test(NumIterations, Gravity);

		Test.ParticlePositions =
		{
			{ (FReal)0, (FReal)0, (FReal)20 * BoxSize },
			{ (FReal)0, (FReal)2 * BoxSize, (FReal)20 * BoxSize },
			{ (FReal)0, (FReal)4 * BoxSize, (FReal)20 * BoxSize },
		};
		Test.ParticleSizes =
		{
			{ (FReal)BoxSize, (FReal)BoxSize, (FReal)BoxSize },
			{ (FReal)BoxSize, (FReal)BoxSize, (FReal)BoxSize },
			{ (FReal)BoxSize, (FReal)BoxSize, (FReal)BoxSize },
		};
		Test.ParticleMasses =
		{
			(FReal)0,
			(FReal)BoxMass,
			(FReal)BoxMass,
		};

		Test.JointPositions =
		{
			Test.ParticlePositions[0],
			Test.ParticlePositions[1],
		};
		Test.JointParticleIndices =
		{
			{ 0, 1 },
			{ 1, 2 },
		};

		Test.Create();

		const FVec3 Box2LocalSpaceJointPosition = Test.JointPositions[0] - Test.ParticlePositions[1];

		FReal MaxDistanceError = 0.0f;
		int32 MaxDistanceErrorFrameIndex = INDEX_NONE;
		for (int32 FrameIndex = 0; FrameIndex < 1000; ++FrameIndex)
		{
			const FReal Time = FrameIndex * Dt;
			const FVec3 RootOffset = FMath::Sin((FReal)2 * PI * Time / AnimPeriod) * AnimDelta;
			const FVec3 RootPosition = Test.ParticlePositions[0] + RootOffset;

			Test.GetParticle(0)->X() = RootPosition;

			Test.Evolution.GetCollisionDetector().GetBroadPhase().SetBoundsVelocityInflation(1);
			Test.Evolution.AdvanceOneTimeStep(Dt);
			Test.Evolution.EndFrame(Dt);

			// Particles should remain fixed distance apart
			for (int JointIndex = 0; JointIndex < Test.JointPositions.Num(); ++JointIndex)
			{
				const int32 ParticleIndex1 = Test.JointParticleIndices[JointIndex][0];
				const int32 ParticleIndex2 = Test.JointParticleIndices[JointIndex][1];
				const FVec3 Delta = Test.GetParticle(ParticleIndex2)->CastToRigidParticle()->P() - Test.GetParticle(ParticleIndex1)->X();
				const float Distance = Delta.Size();
				const float ExpectedDistance = (Test.ParticlePositions[ParticleIndex2] - Test.ParticlePositions[ParticleIndex1]).Size();
				EXPECT_NEAR(Distance, ExpectedDistance, AcceptableDistanceError) << "Joint " << JointIndex << " on frame " << FrameIndex;

				const float DistanceError = FMath::Abs(Distance - ExpectedDistance);
				if (DistanceError > MaxDistanceError)
				{
					MaxDistanceError = DistanceError;
					MaxDistanceErrorFrameIndex = FrameIndex;
				}
			}
		}
		EXPECT_LT(MaxDistanceError, AcceptableDistanceError) << "On frame " << MaxDistanceErrorFrameIndex;
	}

	/**
	 * Pendulum with animated root.
	 */
	void JointConstraint_LongChainAnimated()
	{
		const int NumParticles = 10;
		const int32 NumIterations = 20;
		const FReal Gravity = 980;
		const FReal BoxSize = 100;
		const FReal BoxMass = 1000;
		const FReal Dt = (FReal)1 / 20;
		const FReal AnimPeriod = (FReal)1;
		const FVec3 AnimDelta = FVec3(1 * BoxSize, 0, 0);
		const FReal AcceptableDistanceError = 5;
		const FReal Separation = 2 * BoxSize;
		const FVec3 Begin = FVec3(0, 0, (NumParticles + 10) * Separation);
		const FVec3 Dir = FVec3(0, 1, 0);
		const bool bRandomizeConstraintOrder = true;

		FMath::RandInit(1048604845);

		// Create a chain of connected particles, with particle 0 fixed
		FJointConstraintsTest Test(NumIterations, Gravity);
		for (int32 ParticleIndex = 0; ParticleIndex < NumParticles; ++ParticleIndex)
		{
			Test.ParticlePositions.Add(Begin + ParticleIndex * Separation * Dir);
			Test.ParticleSizes.Add({ BoxSize, BoxSize, BoxSize });
			Test.ParticleMasses.Add((ParticleIndex == 0) ? (FReal)0 : BoxMass);
		}
		for (int32 JointIndex = 0; JointIndex < NumParticles - 1; ++JointIndex)
		{
			Test.JointPositions.Add(Test.ParticlePositions[JointIndex]);
			Test.JointParticleIndices.Add({ JointIndex, JointIndex + 1 });
		}

		// Randomize constraint order
		if (bRandomizeConstraintOrder)
		{
			for (int32 JointIndex = 0; JointIndex < Test.JointParticleIndices.Num(); ++JointIndex)
			{
				const int32 Index0 = JointIndex;
				const int32 Index1 = FMath::RandRange(0, Test.JointParticleIndices.Num() - 1);
				Test.JointPositions.Swap(Index0, Index1);
				Test.JointParticleIndices.Swap(Index0, Index1);
			}
		}

		Test.Create();
		Test.Evolution.GetCollisionDetector().GetBroadPhase().SetBoundsVelocityInflation(1);

		const FVec3 Box2LocalSpaceJointPosition = Test.JointPositions[0] - Test.ParticlePositions[1];

		FReal MaxDistanceError = 0.0f;
		int32 MaxDistanceErrorFrameIndex = INDEX_NONE;
		for (int32 FrameIndex = 0; FrameIndex < 1000; ++FrameIndex)
		{
			const FReal Time = FrameIndex * Dt;
			const FVec3 RootOffset = FMath::Sin((FReal)2 * PI * Time / AnimPeriod) * AnimDelta;
			const FVec3 RootPosition = Test.ParticlePositions[0] + RootOffset;

			Test.GetParticle(0)->X() = RootPosition;

			Test.Evolution.AdvanceOneTimeStep(Dt);
			Test.Evolution.EndFrame(Dt);

			// Particles should remain fixed distance apart
			for (int JointIndex = 0; JointIndex < Test.JointPositions.Num(); ++JointIndex)
			{
				const int32 ParticleIndex1 = Test.JointParticleIndices[JointIndex][0];
				const int32 ParticleIndex2 = Test.JointParticleIndices[JointIndex][1];
				const FVec3 Delta = Test.GetParticle(ParticleIndex2)->CastToRigidParticle()->P() - Test.GetParticle(ParticleIndex1)->X();
				const float Distance = Delta.Size();
				const float ExpectedDistance = (Test.ParticlePositions[ParticleIndex2] - Test.ParticlePositions[ParticleIndex1]).Size();
				EXPECT_NEAR(Distance, ExpectedDistance, AcceptableDistanceError) << "Joint " << JointIndex << " on frame " << FrameIndex;

				// Track largest error that exceeds threshold
				const float DistanceError = FMath::Abs(Distance - ExpectedDistance);
				if (DistanceError > MaxDistanceError)
				{
					MaxDistanceError = DistanceError;
					MaxDistanceErrorFrameIndex = FrameIndex;
				}
			}
		}
		// Report the largest error and when it occurred if it exceeded the threshold
		EXPECT_LT(MaxDistanceError, AcceptableDistanceError) << "On frame " << MaxDistanceErrorFrameIndex;
	}

	void SpringConstraint()
	{
		TUniquePtr<FChaosPhysicsMaterial> PhysicalMaterial = MakeUnique<FChaosPhysicsMaterial>();
		PhysicalMaterial->Friction = 0;
		PhysicalMaterial->Restitution = 0;
		PhysicalMaterial->SleepingLinearThreshold = 0;
		PhysicalMaterial->SleepingAngularThreshold = 0;
		PhysicalMaterial->DisabledLinearThreshold = 0;
		PhysicalMaterial->DisabledAngularThreshold = 0;

		TPBDRigidsSOAs<FReal, 3> Particles;

		auto StaticBox = AppendStaticParticleBox<FReal>(Particles, FVec3((FReal)100, (FReal)100, (FReal)100));
		auto Box2 = AppendDynamicParticleBox<FReal>(Particles, FVec3((FReal)100, (FReal)100, (FReal)100));
		StaticBox->X() = FVec3((FReal)0, (FReal)0, (FReal)1000);

		Box2->X() = FVec3((FReal)500, (FReal)0, (FReal)1000);
		Box2->P() = Box2->X();

		TPBDRigidsEvolutionGBF<FReal, 3> Evolution(Particles);
		TArray<TVector<TGeometryParticleHandle<FReal, 3>*,2>> Constraints = { TVector<TGeometryParticleHandle<FReal,3>*, 2>(StaticBox, Box2) };
		TArray<FVec3> Points0 = { FVec3((FReal)100, (FReal)0, (FReal)1000) };
		TArray<FVec3> Points1 = { FVec3((FReal)400, (FReal)0, (FReal)1000) };

		Evolution.SetPhysicsMaterial(StaticBox, MakeSerializable(PhysicalMaterial));
		Evolution.SetPhysicsMaterial(Box2, MakeSerializable(PhysicalMaterial));

		auto JointConstraints = Chaos::TPBDRigidSpringConstraints<FReal, 3>(Points0, Points1, MoveTemp(Constraints), 1.f);
		auto JointRule = Chaos::TPBDConstraintIslandRule<Chaos::TPBDRigidSpringConstraints<FReal, 3>>(JointConstraints);
		Evolution.AddConstraintRule(&JointRule);

		const FReal Dt = 0.01f;
		for (int32 i = 0; i < 100; ++i)
		{
			Evolution.AdvanceOneTimeStep(Dt);
			Evolution.EndFrame(Dt);
			EXPECT_LT(FMath::Abs((Box2->R().RotateVector(FVec3((FReal)-100, (FReal)0, (FReal)0)) + Box2->X() - Points0[0]).Size() - 300.f), 0.1);
		}
	}

	void DynamicSpringConstraint()
	{
		TUniquePtr<FChaosPhysicsMaterial> PhysicalMaterial = MakeUnique<FChaosPhysicsMaterial>();
		PhysicalMaterial->Friction = 0;
		PhysicalMaterial->Restitution = 0;
		PhysicalMaterial->SleepingLinearThreshold = 0;
		PhysicalMaterial->SleepingAngularThreshold = 0;
		PhysicalMaterial->DisabledLinearThreshold = 0;
		PhysicalMaterial->DisabledAngularThreshold = 0;

		{
			TPBDRigidsSOAs<FReal, 3> Particles;

			auto& StaticBox = *AppendStaticParticleBox<FReal>(Particles, FVec3((FReal)100, (FReal)100, (FReal)100));
			auto& Box2 = *AppendDynamicParticleBox<FReal>(Particles, FVec3((FReal)100, (FReal)100, (FReal)100));
			StaticBox.X() = FVec3((FReal)0, (FReal)0, (FReal)500);

			Box2.X() = FVec3((FReal)500, (FReal)0, (FReal)1000);
			Box2.P() = Box2.X();

			TPBDRigidsEvolutionGBF<FReal, 3> Evolution(Particles);
			TArray<TVector<TGeometryParticleHandle<FReal,3>*, 2>> Constraints = { TVector<TGeometryParticleHandle<FReal,3>*, 2>(&StaticBox, &Box2) };

			Evolution.SetPhysicsMaterial(&StaticBox, MakeSerializable(PhysicalMaterial));
			Evolution.SetPhysicsMaterial(&Box2, MakeSerializable(PhysicalMaterial));

			Chaos::TPBDRigidDynamicSpringConstraints<FReal, 3> SpringConstraints(MoveTemp(Constraints));
			auto SpringRule = Chaos::TPBDConstraintIslandRule<Chaos::TPBDRigidDynamicSpringConstraints<FReal, 3>>(SpringConstraints);
			Evolution.AddConstraintRule(&SpringRule);

			const FReal Dt = 0.01f;
			for (int32 i = 0; i < 200; ++i)
			{
				Evolution.AdvanceOneTimeStep(Dt);
				Evolution.EndFrame(Dt);
			}
			EXPECT_LT(Box2.X()[2], 0);
		}

		{
			TPBDRigidsSOAs<FReal, 3> Particles;

			auto& StaticBox = *AppendStaticParticleBox<FReal>(Particles, FVec3((FReal)100, (FReal)100, (FReal)100));
			auto& Box2 = *AppendDynamicParticleBox<FReal>(Particles, FVec3((FReal)100, (FReal)100, (FReal)100));
			StaticBox.X() = FVec3((FReal)0, (FReal)0, (FReal)500);

			Box2.X() = FVec3((FReal)500, (FReal)0, (FReal)1000);
			Box2.P() = Box2.X();

			TPBDRigidsEvolutionGBF<FReal, 3> Evolution(Particles);
			TArray<TVector<TGeometryParticleHandle<FReal,3>*, 2>> Constraints = { TVector<TGeometryParticleHandle<FReal,3>*, 2>(&StaticBox, &Box2) };

			Evolution.SetPhysicsMaterial(&StaticBox, MakeSerializable(PhysicalMaterial));
			Evolution.SetPhysicsMaterial(&Box2, MakeSerializable(PhysicalMaterial));

			Chaos::TPBDRigidDynamicSpringConstraints<FReal, 3> SpringConstraints(MoveTemp(Constraints), 400);
			auto SpringRule = Chaos::TPBDConstraintIslandRule<Chaos::TPBDRigidDynamicSpringConstraints<FReal, 3>>(SpringConstraints);
			Evolution.AddConstraintRule(&SpringRule);

			const FReal Dt = 0.01f;
			for (int32 i = 0; i < 200; ++i)
			{
				Evolution.AdvanceOneTimeStep(Dt);
				Evolution.EndFrame(Dt);
			}
			EXPECT_GT(Box2.X()[2], 0);
		}
	}



	TEST(JointTests, TestSingleConstraint) {
		JointConstraint_Single();
	}

	TEST(JointTests, TestSingleConstraintWithLateralTranslation) {
		JointConstraint_SingleMoveRoot();
	}

	TEST(JointTests, TestSingleConstraintWithAnimatedRoot) {
		JointConstraint_SingleAnimated();
	}

	TEST(JointTests, TestShortJointChainWithAnimatedRoot) {
		JointConstraint_ShortChainAnimated();
	}

	TEST(JointTests, TestLongJointChainWithAnimatedRoot) {
		JointConstraint_LongChainAnimated();
	}

	TEST(JointTests, TestSingleSpringConstraint) {
		SpringConstraint();
	}

	TEST(JointTests, TestSingleDynamicSpringConstraint) {
		DynamicSpringConstraint();
	}
}

