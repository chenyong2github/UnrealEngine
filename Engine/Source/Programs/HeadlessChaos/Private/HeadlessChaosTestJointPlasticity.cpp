// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaos.h"
#include "HeadlessChaosTestJoint.h"
#include "Modules/ModuleManager.h"

namespace ChaosTest {

	using namespace Chaos;


	// 1 Kinematic Body with 1 Dynamic body held horizontally by a plastic angular constraint.
	// Constraint plasticity limit is larger than resulting rotational settling so constraint will not bend.
	template <typename TEvolution>
	void JointPlasticity_UnderAngularPlasticityThreshold()
	{
		const FReal PlasticityAngle = 10;
		const int32 NumIterations = 1;
		const FReal Gravity = 980;
		const FReal Dt = 0.01f;
		int32 NumIts = 100;

		FJointChainTest<TEvolution> Test(NumIterations, Gravity);
		Test.InitChain(2, FVec3(0, 1, 0));

		// Joint should break only if Threshold < MGL
		// So not in this test
		Test.JointSettings[0].bCollisionEnabled = false;
		Test.JointSettings[0].LinearMotionTypes = { EJointMotionType::Locked, EJointMotionType::Locked , EJointMotionType::Locked };

		Test.JointSettings[0].bAngularSLerpPositionDriveEnabled = true;
		Test.JointSettings[0].bAngularSLerpVelocityDriveEnabled = true;
		Test.JointSettings[0].AngularDriveDamping = 500;
		Test.JointSettings[0].AngularDriveStiffness = 500000.f;
		
		Test.JointSettings[0].AngularPlasticityLimit = PlasticityAngle * (PI/180.);

		Test.Create();
		Test.AddParticleBox(FVec3(0, 30, 50), FRotation3::Identity, FVec3(10.f), 100.f);

		FReal Angle = Test.Joints.GetConstraintSettings(0).AngularDrivePositionTarget.GetAngle() * (180. / PI);
		EXPECT_TRUE(FMath::IsNearlyEqual(Angle, 0.f));

		// Run the sim
		for (int32 i = 0; i < NumIts; ++i)
		{

			Test.Evolution.AdvanceOneTimeStep(Dt);
			Test.Evolution.EndFrame(Dt);

			//FReal Angle = Test.Joints.GetConstraintSettings(0).AngularDrivePositionTarget.GetAngle() * (180. / PI);
			//FVec3 Pos = Test.SOAs.GetDynamicParticles().X(0);
			//std::cout << "["<< Angle <<"]" << Pos.X << "," << Pos.Y << "," << Pos.Z << std::endl;
		}
		Angle = Test.Joints.GetConstraintSettings(0).AngularDrivePositionTarget.GetAngle() * (180. / PI);

		// Nothing should have broken
		EXPECT_TRUE(FMath::IsNearlyEqual(Angle, 0.f));
		
	}

	TYPED_TEST(AllEvolutions, JointPlasticity_UnderAngularPlasticityThreshold)
	{
		JointPlasticity_UnderAngularPlasticityThreshold<TypeParam>();
	}


	// 1 Kinematic Body with 1 Dynamic body held horizontally by a plastic angular constraint.
	// Constraint plasticity limit is larger than resulting rotational settling so constraint will not bend.
	template <typename TEvolution>
	void JointPlasticity_OverAngularPlasticityThreshold()
	{
		const FReal PlasticityAngle = 10;
		const int32 NumIterations = 1;
		const FReal Gravity = 980;
		const FReal Dt = 0.01f;
		int32 NumIts = 100;

		FJointChainTest<TEvolution> Test(NumIterations, Gravity);
		Test.InitChain(2, FVec3(0, 1, 0));

		// Joint should break only if Threshold < MGL
		// So not in this test
		Test.JointSettings[0].bCollisionEnabled = false;
		Test.JointSettings[0].LinearMotionTypes = { EJointMotionType::Locked, EJointMotionType::Locked , EJointMotionType::Locked };

		Test.JointSettings[0].bAngularSLerpPositionDriveEnabled = true;
		Test.JointSettings[0].bAngularSLerpVelocityDriveEnabled = true;
		Test.JointSettings[0].AngularDriveDamping = 500;
		Test.JointSettings[0].AngularDriveStiffness = 50000.f;

		Test.JointSettings[0].AngularPlasticityLimit = PlasticityAngle * (PI / 180.);

		Test.Create();
		Test.AddParticleBox(FVec3(0, 30, 50), FRotation3::Identity, FVec3(10.f), 100.f);

		FReal Angle = Test.Joints.GetConstraintSettings(0).AngularDrivePositionTarget.GetAngle() * (180. / PI);
		EXPECT_TRUE(FMath::IsNearlyEqual(Angle, 0.f));

		// Run the sim
		for (int32 i = 0; i < NumIts; ++i)
		{

			Test.Evolution.AdvanceOneTimeStep(Dt);
			Test.Evolution.EndFrame(Dt);

			//FReal Angle = Test.Joints.GetConstraintSettings(0).AngularDrivePositionTarget.GetAngle() * (180. / PI);
			//FVec3 Pos = Test.SOAs.GetDynamicParticles().X(0);
			//std::cout << "["<< Angle <<"]" << Pos.X << "," << Pos.Y << "," << Pos.Z << std::endl;
		}
		Angle = Test.Joints.GetConstraintSettings(0).AngularDrivePositionTarget.GetAngle() * (180. / PI);

		// Nothing should have broken
		EXPECT_FALSE(FMath::IsNearlyEqual(Angle, 0.f));
		EXPECT_TRUE(FMath::IsNearlyEqual(Angle, PlasticityAngle, PlasticityAngle * 0.1f));

	}

	TYPED_TEST(AllEvolutions, JointPlasticity_OverAngularPlasticityThreshold)
	{
		JointPlasticity_OverAngularPlasticityThreshold<TypeParam>();
	}

}