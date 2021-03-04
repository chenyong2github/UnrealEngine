// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/Matrix.h"
#include "Chaos/Utilities.h"
#include "Chaos/AABB.h"
#include "Chaos/Core.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/Box.h"
#include "Chaos/Sphere.h"
#include "ChaosSolversModule.h"
#include "HeadlessChaos.h"
#include "HeadlessChaosTestUtility.h"
#include "Modules/ModuleManager.h"
#include "PBDRigidsSolver.h"

//PRAGMA_DISABLE_OPTIMIZATION

namespace ChaosTest
{
	using namespace Chaos;

	// CCD not implemented for sphere sphere
	TYPED_TEST(AllEvolutions, DISABLED_CCDTests_CCDEnabled)
	{
		const FReal Dt = 1 / 30.0f;
		const FReal Fps = 1 / Dt;
		const FReal SphereRadius = 100; // cm
		const FReal InitialSpeed = SphereRadius * 5 * Fps; // More than enough to tunnel
		using TEvolution = TypeParam;
		TPBDRigidsSOAs<FReal, 3> Particles;
		THandleArray<FChaosPhysicsMaterial> PhysicalMaterials;
		TEvolution Evolution(Particles, PhysicalMaterials);
		InitEvolutionSettings(Evolution);

		// Create particles
		TGeometryParticleHandle<FReal, 3>* Static = Evolution.CreateStaticParticles(1)[0];
		TPBDRigidParticleHandle<FReal, 3>* Dynamic = Evolution.CreateDynamicParticles(1)[0];

		// Set up physics material
		TUniquePtr<FChaosPhysicsMaterial> PhysicsMaterial = MakeUnique<FChaosPhysicsMaterial>();
		PhysicsMaterial->SleepCounterThreshold = 1000; // Don't sleep
		PhysicsMaterial->Restitution = 1.0f;

		// Create Sphere geometry (Radius = 100)
		TUniquePtr<FImplicitObject> Sphere(new TSphere<FReal, 3>(FVec3(0, 0, 0), SphereRadius));

		// Assign sphere geometry to both particles 
		Static->SetGeometry(MakeSerializable(Sphere));
		Dynamic->SetGeometry(MakeSerializable(Sphere));

		Evolution.SetPhysicsMaterial(Dynamic, MakeSerializable(PhysicsMaterial));

		const FReal Mass = 100000.0f;;
		Dynamic->I() = FMatrix33(Mass, Mass, Mass);
		Dynamic->InvI() = FMatrix33(1.0f / Mass, 1.0f / Mass, 1.0f / Mass);

		// Positions and velocities
		Static->X() = FVec3(0, 0, 0);

		Dynamic->X() = FVec3(0, 0, SphereRadius * 2 + 10);
		//Dynamic->V() = FVec3(0, 0, -InitialSpeed);
		
		// The position of the static has changed and statics don't automatically update bounds, so update explicitly
		Static->SetWorldSpaceInflatedBounds(Sphere->BoundingBox().TransformedAABB(TRigidTransform<FReal, 3>(Static->X(), Static->R())));

		::ChaosTest::SetParticleSimDataToCollide({ Static,Dynamic });

		Dynamic->SetCCDEnabled(true);
		Dynamic->SetGravityEnabled(false);
		

		Dynamic->V() = FVec3(0, 0, -InitialSpeed);

		for (int i = 0; i < 1; ++i)
		{
			Evolution.AdvanceOneTimeStep(Dt);
			Evolution.EndFrame(Dt);
		}

		// Large error margin, we are testing CCD and not solver accuracy
		const FReal LargeErrorMargin = 10.0f;
		EXPECT_GE(Dynamic->X()[2], SphereRadius * 2 - LargeErrorMargin);
	}



	TYPED_TEST(AllEvolutions,DISABLED_CCDTests_CCDDisabled)
	{
		const FReal Dt = 1 / 30.0f;
		const FReal Fps = 1 / Dt;
		const FReal SphereRadius = 100; // cm
		const FReal InitialSpeed = SphereRadius * 10 * Fps; // More than enough to tunnel
		using TEvolution = TypeParam;
		TPBDRigidsSOAs<FReal, 3> Particles;
		THandleArray<FChaosPhysicsMaterial> PhysicalMaterials;
		TEvolution Evolution(Particles, PhysicalMaterials);
		InitEvolutionSettings(Evolution);

		// Create particles
		TGeometryParticleHandle<FReal, 3>* Static = Evolution.CreateStaticParticles(1)[0];
		TPBDRigidParticleHandle<FReal, 3>* Dynamic = Evolution.CreateDynamicParticles(1)[0];

		// Set up physics material
		TUniquePtr<FChaosPhysicsMaterial> PhysicsMaterial = MakeUnique<FChaosPhysicsMaterial>();
		PhysicsMaterial->SleepCounterThreshold = 1000; // Don't sleep
		PhysicsMaterial->Restitution = 1.0f;

		// Create Sphere geometry (Radius = 100)
		TUniquePtr<FImplicitObject> Sphere(new TSphere<FReal, 3>(FVec3(0, 0, 0), SphereRadius));

		// Assign sphere geometry to both particles 
		Static->SetGeometry(MakeSerializable(Sphere));
		Dynamic->SetGeometry(MakeSerializable(Sphere));

		Evolution.SetPhysicsMaterial(Dynamic, MakeSerializable(PhysicsMaterial));

		const FReal Mass = 100000.0f;;
		Dynamic->I() = FMatrix33(Mass, Mass, Mass);
		Dynamic->InvI() = FMatrix33(1.0f / Mass, 1.0f / Mass, 1.0f / Mass);

		// Positions and velocities
		Static->X() = FVec3(0, 0, 0);

		Dynamic->X() = FVec3(0, 0, SphereRadius * 2 + 100);

		// The position of the static has changed and statics don't automatically update bounds, so update explicitly
		Static->SetWorldSpaceInflatedBounds(Sphere->BoundingBox().TransformedAABB(TRigidTransform<FReal, 3>(Static->X(), Static->R())));

		::ChaosTest::SetParticleSimDataToCollide({ Static,Dynamic });

		Dynamic->SetCCDEnabled(false);
		Dynamic->SetGravityEnabled(false);

		Dynamic->V() = FVec3(0, -InitialSpeed, 0);

		for (int i = 0; i < 1; ++i)
		{
			Evolution.AdvanceOneTimeStep(Dt);
			Evolution.EndFrame(Dt);
		}

		// Large error margin, we are testing CCD and not solver accuracy
		const FReal LargeErrorMargin = 10.0f;
		EXPECT_NEAR(Dynamic->X()[2], SphereRadius * 2 + 10 - InitialSpeed * Dt, LargeErrorMargin);
	}

	
	TYPED_TEST(AllEvolutions, DISABLED_CCDTests_BoxStayInsideBoxBoundaries)
	{
		const FReal Dt = 1 / 30.0f;
		const FReal Fps = 1 / Dt;
		const FReal SmallBoxSize = 100; // cm
		const FReal ContainerBoxSize = 500; // cm
		const FReal ContainerWallThickness = 10; // cm
		const int ContainerFaceCount = 6;

		const FVec3 InitialVelocity = FVec3(0, 0, 750); // ContainerBoxSize * 5 * Fps; // More than enough to tunnel

		

		using TEvolution = TypeParam;
		TPBDRigidsSOAs<FReal, 3> Particles;
		THandleArray<FChaosPhysicsMaterial> PhysicalMaterials;
		TEvolution Evolution(Particles, PhysicalMaterials);
		InitEvolutionSettings(Evolution);

		// Create particles
		TArray <TGeometryParticleHandle<FReal, 3>*> ContainerFaces = Evolution.CreateStaticParticles(ContainerFaceCount); // 6 sides of box
		TPBDRigidParticleHandle<FReal, 3>* Dynamic = Evolution.CreateDynamicParticles(1)[0]; // The small box

		// Set up physics material
		TUniquePtr<FChaosPhysicsMaterial> PhysicsMaterial = MakeUnique<FChaosPhysicsMaterial>();
		PhysicsMaterial->SleepCounterThreshold = 1000; // Don't sleep
		PhysicsMaterial->Restitution = 0.0f;

		// Create box geometry 
		TUniquePtr<FImplicitObject> SmallBox(new TBox<FReal, 3>(FVec3(-SmallBoxSize/2, -SmallBoxSize/2, -SmallBoxSize/2), FVec3(SmallBoxSize/2, SmallBoxSize/2, SmallBoxSize/2)));

		// Just use 3 (x2) boxes for the walls of the container (avoid rotation transforms for this test)
		TUniquePtr<FImplicitObject> ContainerFaceX(new TBox<FReal, 3>(FVec3(-ContainerWallThickness / 2, -ContainerBoxSize / 2, -ContainerBoxSize / 2), FVec3(ContainerWallThickness / 2, ContainerBoxSize / 2, ContainerBoxSize / 2)));
		TUniquePtr<FImplicitObject> ContainerFaceY(new TBox<FReal, 3>(FVec3(-ContainerBoxSize / 2, -ContainerWallThickness / 2, -ContainerBoxSize / 2), FVec3(ContainerBoxSize / 2, ContainerWallThickness / 2, ContainerBoxSize / 2)));
		TUniquePtr<FImplicitObject> ContainerFaceZ(new TBox<FReal, 3>(FVec3(-ContainerBoxSize / 2, -ContainerBoxSize / 2, -ContainerWallThickness / 2), FVec3(ContainerBoxSize / 2, ContainerBoxSize / 2, ContainerWallThickness / 2)));		

		// Assign geometry to all particles 
		Dynamic->SetGeometry(MakeSerializable(SmallBox));

		ContainerFaces[0]->SetGeometry(MakeSerializable(ContainerFaceX));
		ContainerFaces[1]->SetGeometry(MakeSerializable(ContainerFaceX));
		ContainerFaces[2]->SetGeometry(MakeSerializable(ContainerFaceY));
		ContainerFaces[3]->SetGeometry(MakeSerializable(ContainerFaceY));
		ContainerFaces[4]->SetGeometry(MakeSerializable(ContainerFaceZ));
		ContainerFaces[5]->SetGeometry(MakeSerializable(ContainerFaceZ));
		

		Evolution.SetPhysicsMaterial(Dynamic, MakeSerializable(PhysicsMaterial));

		const FReal Mass = 100000.0f;
		Dynamic->I() = FMatrix33(Mass, Mass, Mass);
		Dynamic->InvI() = FMatrix33(1.0f / Mass, 1.0f / Mass, 1.0f / Mass);

		// Positions and velocities
		ContainerFaces[0]->X() = FVec3(ContainerBoxSize / 2, 0, 0);
		ContainerFaces[1]->X() = FVec3(-ContainerBoxSize / 2, 0, 0);
		ContainerFaces[2]->X() = FVec3(0, ContainerBoxSize / 2, 0);
		ContainerFaces[3]->X() = FVec3(0, -ContainerBoxSize / 2, 0);
		ContainerFaces[4]->X() = FVec3(0, 0, ContainerBoxSize / 2);
		ContainerFaces[5]->X() = FVec3(0, 0, -ContainerBoxSize / 2);

		Dynamic->X() = FVec3(0, 0, 0);

		// The position of the static has changed and statics don't automatically update bounds, so update explicitly
		ContainerFaces[0]->SetWorldSpaceInflatedBounds(ContainerFaceX->BoundingBox().TransformedAABB(TRigidTransform<FReal, 3>(ContainerFaces[0]->X(), ContainerFaces[0]->R())));
		ContainerFaces[1]->SetWorldSpaceInflatedBounds(ContainerFaceX->BoundingBox().TransformedAABB(TRigidTransform<FReal, 3>(ContainerFaces[1]->X(), ContainerFaces[1]->R())));
		ContainerFaces[2]->SetWorldSpaceInflatedBounds(ContainerFaceY->BoundingBox().TransformedAABB(TRigidTransform<FReal, 3>(ContainerFaces[2]->X(), ContainerFaces[2]->R())));
		ContainerFaces[3]->SetWorldSpaceInflatedBounds(ContainerFaceY->BoundingBox().TransformedAABB(TRigidTransform<FReal, 3>(ContainerFaces[3]->X(), ContainerFaces[3]->R())));
		ContainerFaces[4]->SetWorldSpaceInflatedBounds(ContainerFaceZ->BoundingBox().TransformedAABB(TRigidTransform<FReal, 3>(ContainerFaces[4]->X(), ContainerFaces[4]->R())));
		ContainerFaces[5]->SetWorldSpaceInflatedBounds(ContainerFaceZ->BoundingBox().TransformedAABB(TRigidTransform<FReal, 3>(ContainerFaces[5]->X(), ContainerFaces[5]->R())));

		::ChaosTest::SetParticleSimDataToCollide({ Dynamic });
		::ChaosTest::SetParticleSimDataToCollide({ ContainerFaces });

		Dynamic->SetCCDEnabled(true);
		Dynamic->SetGravityEnabled(false);

		Dynamic->V() = InitialVelocity;

		for (int i = 0; i < 10; ++i)
		{
			Evolution.AdvanceOneTimeStep(Dt);
			Evolution.EndFrame(Dt);
		}

		// Large error margin, we are testing CCD and not solver accuracy
		const FReal LargeErrorMargin = 10.0f;
		// Check that we did not escape the box!
		for (int axis = 0; axis < 3; axis++)
		{
			// If this failed, the dynamic cube escaped the air tight static container
			EXPECT_LT(FMath::Abs(Dynamic->X()[axis]), ContainerBoxSize / 2 - ContainerWallThickness / 2 - SmallBoxSize / 2 + LargeErrorMargin);
		}
	}

	// CCD not implemented for sphere sphere
	TYPED_TEST(AllEvolutions, DISABLED_CCDTests_Shere_Sphere)
	{
		const FReal Dt = 1 / 30.0f;
		const FReal Fps = 1 / Dt;
		const FReal SphereRadius = 100; // cm
		const FReal InitialSpeed = SphereRadius * 5 * Fps; // More than enough to tunnel
		using TEvolution = TypeParam;
		TPBDRigidsSOAs<FReal, 3> Particles;
		THandleArray<FChaosPhysicsMaterial> PhysicalMaterials;
		TEvolution Evolution(Particles, PhysicalMaterials);
		InitEvolutionSettings(Evolution);

		// Create particles
		TGeometryParticleHandle<FReal, 3>* Static = Evolution.CreateStaticParticles(1)[0];
		TPBDRigidParticleHandle<FReal, 3>* Dynamic = Evolution.CreateDynamicParticles(1)[0];

		// Set up physics material
		TUniquePtr<FChaosPhysicsMaterial> PhysicsMaterial = MakeUnique<FChaosPhysicsMaterial>();
		PhysicsMaterial->SleepCounterThreshold = 1000; // Don't sleep
		PhysicsMaterial->Restitution = 1.0f;

		// Create Sphere geometry (Radius = 100)
		TUniquePtr<FImplicitObject> Sphere(new TSphere<FReal, 3>(FVec3(0, 0, 0), SphereRadius));

		// Assign sphere geometry to both particles 
		Static->SetGeometry(MakeSerializable(Sphere));
		Dynamic->SetGeometry(MakeSerializable(Sphere));

		Evolution.SetPhysicsMaterial(Dynamic, MakeSerializable(PhysicsMaterial));

		const FReal Mass = 100000.0f;;
		Dynamic->I() = FMatrix33(Mass, Mass, Mass);
		Dynamic->InvI() = FMatrix33(1.0f / Mass, 1.0f / Mass, 1.0f / Mass);

		// Positions and velocities
		Static->X() = FVec3(0, 0, 0);

		Dynamic->X() = FVec3(0, 0, SphereRadius * 2 + 10);
		//Dynamic->V() = FVec3(0, 0, -InitialSpeed);

		// The position of the static has changed and statics don't automatically update bounds, so update explicitly
		Static->SetWorldSpaceInflatedBounds(Sphere->BoundingBox().TransformedAABB(TRigidTransform<FReal, 3>(Static->X(), Static->R())));

		::ChaosTest::SetParticleSimDataToCollide({ Static,Dynamic });

		Dynamic->SetCCDEnabled(true);
		Dynamic->SetGravityEnabled(false);


		Dynamic->V() = FVec3(0, 0, -InitialSpeed);

		for (int i = 0; i < 1; ++i)
		{
			Evolution.AdvanceOneTimeStep(Dt);
			Evolution.EndFrame(Dt);
		}

		// Large error margin, we are testing CCD and not solver accuracy
		const FReal LargeErrorMargin = 10.0f;
		EXPECT_GE(Dynamic->X()[2], SphereRadius * 2 - LargeErrorMargin);
	}
}

