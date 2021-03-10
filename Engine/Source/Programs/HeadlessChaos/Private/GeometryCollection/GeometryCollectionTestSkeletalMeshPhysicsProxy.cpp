// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionTestSkeletalMeshPhysicsProxy.h"

#include "Chaos/Sphere.h"
#include "PhysicsSolver.h"
#include "PhysicsProxy/PhysicsProxies.h"
#include "ChaosSolversModule.h"
#include "Chaos/ParticleHandle.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollectionProxyData.h"
#include "GeometryCollection/GeometryCollectionTestFramework.h"
#include "GeometryCollection/GeometryCollectionTestUtility.h"

#include "gtest/gtest.h"


namespace GeometryCollectionTest
{

	using namespace Chaos;

	/**
	 * Create an empty SkeletalMeshPhysicsProxy and register it with a solver.
	 * Check that the appropriate callbacks are called and nothing crashes.
	 */
#if 0

	void TestSkeletalMeshPhysicsProxy_Register()
	{
		//TODO: this code has not been called in a while, but it was templated so it compiled
		const FReal Dt = (FReal)1 / 60;

		FrameworkParameters Fp;
		Fp.Dt = Dt;
		FFramework UnitTest(Fp);

		int32 InitCallCount = 0;
		int32 InputCallCount = 0;
		auto InitFunc = [&](FSkeletalMeshPhysicsProxyParams & OutPhysicsParams) -> void
		{
			++InitCallCount;
		};
		auto InputFunc = [&](const FReal Dt, FSkeletalMeshPhysicsProxyParams& OutPhysicsParams) -> bool
		{
			++InputCallCount;
			return true;
		};

		FSkeletalMeshPhysicsProxy SkeletalMeshPhysicsProxy(nullptr, InitFunc);		

		EXPECT_EQ(InitCallCount, 0);
		EXPECT_EQ(InputCallCount, 0);
		EXPECT_EQ(SkeletalMeshPhysicsProxy.GetOutputs(), nullptr);

		// todo
		// UnitTest.Solver->RegisterObject(&SkeletalMeshPhysicsProxy);


		// Should call Init func
		SkeletalMeshPhysicsProxy.Initialize();
		EXPECT_EQ(InitCallCount, 1);
		EXPECT_EQ(InputCallCount, 0);
		EXPECT_EQ(SkeletalMeshPhysicsProxy.GetOutputs(), nullptr);

		UnitTest.Initialize();
		{
			// Should call Input func
			SkeletalMeshPhysicsProxy.CaptureInputs(Dt, InputFunc);
			EXPECT_EQ(InitCallCount, 1);
			EXPECT_EQ(InputCallCount, 1);
			EXPECT_EQ(SkeletalMeshPhysicsProxy.GetOutputs(), nullptr);

			UnitTest.Advance();			

			// Should give us some outputs
			SkeletalMeshPhysicsProxy.BufferPhysicsResults();
			SkeletalMeshPhysicsProxy.FlipBuffer();
			SkeletalMeshPhysicsProxy.PullFromPhysicsState();
			EXPECT_NE(SkeletalMeshPhysicsProxy.GetOutputs(), nullptr);
		}
	}
#endif

	/**
	 * A minimal class representing a component that uses the SkeletalMeshPhysicsProxy to
	 * implements its physics. E.g., USkeletalMeshComponent or FAnimNode_RigidBody
	 */
	class TFakeSkeletalMeshPhysicsComponent
	{
	public:
		TSharedPtr<FSkeletalMeshPhysicsProxy> SkeletalMeshPhysicsProxy;		

		FReal BoneRadius = 50.0f;
		EObjectStateTypeEnum ObjectState = EObjectStateTypeEnum::Chaos_Object_Kinematic;
		TArray<int32> Parents;
		TArray< EObjectStateTypeEnum> BoneStates;

		FFramework UnitTest;

		// Local-space transform inputs to physics (from animation)
		TArray<FTransform> InputWorldTransforms;
		TArray<FVector> InputLinearVelocities;
		TArray<FVector> InputAngularVelocities;

		// World-space transform outputs from physics (to animation)
		TArray<FTransform> OutputWorldTransforms;
		TArray<FVector> OutputLinearVelocities;
		TArray<FVector> OutputAngularVelocities;


		TFakeSkeletalMeshPhysicsComponent(FReal dt)
		: UnitTest(FrameworkParameters(dt))
		{			
			SkeletalMeshPhysicsProxy = MakeShared<FSkeletalMeshPhysicsProxy>(
				nullptr,
				[this](FSkeletalMeshPhysicsProxyParams & OutPhysicsParams) -> void
				{
					InitCallback(OutPhysicsParams);
				});
		}

		void InitCallback(FSkeletalMeshPhysicsProxyParams & OutParams)
		{
			// Check setup is valid
			check(Parents.Num() == InputWorldTransforms.Num());

			// Fill in data that was not provided
			InputLinearVelocities.SetNumZeroed(Parents.Num());
			InputAngularVelocities.SetNumZeroed(Parents.Num());
			while (BoneStates.Num() < Parents.Num())
			{
				BoneStates.Add(EObjectStateTypeEnum::Chaos_Object_UserDefined);
			}

			// Build the bone hierarchy
			FBoneHierarchy Hierarchy;
			{
				// Build rigid body setup
				Hierarchy.InitPreAdd(InputWorldTransforms.Num());
				for (int32 BoneIndex = 0; BoneIndex < InputWorldTransforms.Num(); ++BoneIndex)
				{
					const FName NAME_Bone = FName(*FString::Printf(TEXT("Bone_%03d"), BoneIndex));
					TUniquePtr<FAnalyticImplicitGroup> Group(new FAnalyticImplicitGroup(NAME_Bone, BoneIndex));
					Group->SetParentBoneIndex(Parents[BoneIndex]);
					Group->Add(FTransform::Identity, new TSphere<FReal, 3>(FVec3(0), BoneRadius));
					Group->SetRigidBodyState(BoneStates[BoneIndex]);
					Hierarchy.Add(MoveTemp(Group));
				}
				Hierarchy.InitPostAdd();

				// Set initial transforms
				Hierarchy.PrepareForUpdate();
				for (int32 BoneIndex = 0; BoneIndex < InputWorldTransforms.Num(); ++BoneIndex)
				{
					const int32 ParentBoneIndex = Parents[BoneIndex];
					FTransform ParentTransform = (ParentBoneIndex >= 0)? InputWorldTransforms[ParentBoneIndex] : InputWorldTransforms[0];
					FTransform LocalTransform = InputWorldTransforms[BoneIndex] * ParentTransform.Inverse();
					Hierarchy.SetAnimLocalSpaceTransform(BoneIndex, LocalTransform);
				}
				Hierarchy.SetActorWorldSpaceTransform(InputWorldTransforms[0]);
				Hierarchy.PrepareAnimWorldSpaceTransforms();
			}

			// Set up the params structure
			OutParams.Name = TEXT("Test_Solve");
			OutParams.InitialTransform = InputWorldTransforms[0];
			OutParams.InitialLinearVelocity = FVector::ZeroVector;
			OutParams.InitialAngularVelocity = FVector::ZeroVector;
			OutParams.BoneHierarchy = MoveTemp(Hierarchy);
			OutParams.ObjectType = ObjectState;
			OutParams.bSimulating = true;
		};

		void Initialize()
		{

			SkeletalMeshPhysicsProxy->Initialize();

			//RigidBodyWrapper* Floor = NewSimulationObject<GeometryType::RigidFloor>()->As<RigidBodyWrapper>();
			//UnitTest.AddSimulationObject(Floor);

			// todo
			// UnitTest.Solver->RegisterObject(SkeletalMeshPhysicsProxy.Get());
		}

		void Uninitialize()
		{
			// todo
			// UnitTest.Solver->UnregisterObject(SkeletalMeshPhysicsProxy.Get());
		}

		void Tick(FReal Dt)
		{
			{
				SkeletalMeshPhysicsProxy->CaptureInputs(Dt, 
					[this](const FReal Dt, FSkeletalMeshPhysicsProxyParams & OutParams) -> bool
					{
						FBoneHierarchy& Hierarchy = OutParams.BoneHierarchy;
						Hierarchy.PrepareForUpdate();
						for (int32 BoneIndex = 0; BoneIndex < InputWorldTransforms.Num(); ++BoneIndex)
						{
							const int32 ParentBoneIndex = Parents[BoneIndex];
							FTransform ParentTransform = (ParentBoneIndex >= 0) ? InputWorldTransforms[ParentBoneIndex] : InputWorldTransforms[0];
							FTransform LocalTransform = InputWorldTransforms[BoneIndex].GetRelativeTransform(ParentTransform);
							Hierarchy.SetAnimLocalSpaceTransform(BoneIndex, LocalTransform);
						}
						Hierarchy.SetActorWorldSpaceTransform(InputWorldTransforms[0]);
						Hierarchy.PrepareAnimWorldSpaceTransforms();
						return true;
					});
								
				UnitTest.Advance();

				if (const FSkeletalMeshPhysicsProxyOutputs * PhysicsOutputs = SkeletalMeshPhysicsProxy->GetOutputs())
				{
					OutputWorldTransforms = PhysicsOutputs->Transforms;
					OutputLinearVelocities = PhysicsOutputs->LinearVelocities;
					OutputAngularVelocities = PhysicsOutputs->AngularVelocities;
				};
			}
		}
	};

	/**
	 * Check that the SkeletalMeshPhysicsProxy is able to provide input and receive correct simulated output from the Solver.
	 * Check that kinematic body state is correctly reproduces the input animation pose.
	 */
	void TestSkeletalMeshPhysicsProxy_Kinematic()
	{
		const FReal Dt = (FReal)1 / 30;

		// Two kinematic bodies
		TFakeSkeletalMeshPhysicsComponent Component(Dt);
		Component.InputWorldTransforms =
		{
			FTransform(FVector(0, 0, 100)),
			FTransform(FVector(0, 100, 150)),
		};
		Component.Parents =
		{
			INDEX_NONE,
			0,
		};

		Component.Initialize();
		

		FReal Time = (FReal)0;

		for (int32 TickIndex = 0; TickIndex < 100; ++TickIndex)
		{
			// Animate both bodies
			FVector Offset0 = 100.0f * FMath::Sin(2 * PI * Time / 1.0f) * FVector(1, 0, 0);
			Component.InputWorldTransforms[0] = FTransform(Offset0) * FTransform(FVector(0, 0, 100));
			Component.InputWorldTransforms[1] = FTransform(FVector(0, 100, 150)) * Component.InputWorldTransforms[0];

			Component.Tick(Dt);

			// All bodies are kinematic. The output pose should match the input pose
			for (int32 TransformIndex = 0; TransformIndex < Component.InputWorldTransforms.Num(); ++TransformIndex)
			{
				EXPECT_NEAR(Component.OutputWorldTransforms[0].GetTranslation().X, Component.InputWorldTransforms[0].GetTranslation().X, KINDA_SMALL_NUMBER);
				EXPECT_NEAR(Component.OutputWorldTransforms[0].GetTranslation().Y, Component.InputWorldTransforms[0].GetTranslation().Y, KINDA_SMALL_NUMBER);
				EXPECT_NEAR(Component.OutputWorldTransforms[0].GetTranslation().Z, Component.InputWorldTransforms[0].GetTranslation().Z, KINDA_SMALL_NUMBER);
			}

			Time += Dt;
		}
	}

	/**
	 * Check that the SkeletalMeshPhysicsProxy is able to provide input and receive correct simulated output from the Solver.
	 * Check that kinematic and dynamic body state is correctly reproduces the input animation pose.
	 */
	void TestSkeletalMeshPhysicsProxy_Dynamic()
	{
		const FReal Dt = (FReal)1 / 30;

		// One kinematic, one dynamic body
		TFakeSkeletalMeshPhysicsComponent Component(Dt);
		
		Component.ObjectState = EObjectStateTypeEnum::Chaos_Object_Dynamic;
		Component.InputWorldTransforms =
		{
			FTransform(FVector(0, 0, 300)),
			//FTransform(FVector(0, 100, 350)),
			FTransform(FVector(0, 200, 300)),
		};
		Component.Parents =
		{
			INDEX_NONE,
			0,
		};
		Component.BoneStates =
		{
			EObjectStateTypeEnum::Chaos_Object_Kinematic,
			EObjectStateTypeEnum::Chaos_Object_Dynamic,
		};
		
		// todo
		// Component.NumIterations = 6;

		Component.Initialize();

		const TArray<FTransform> InitialTransforms = Component.InputWorldTransforms;
		const FReal InitialDistance = (InitialTransforms[1].GetTranslation() - InitialTransforms[0].GetTranslation()).Size();
		

		FReal Time = (FReal)0;

		for (int32 TickIndex = 0; TickIndex < 100; ++TickIndex)
		{
			// Amimate both bodies. Without pose matching or constraints, only the kinematic body should be affected
			FVector Offset0 = 500.0f * FMath::Sin(2 * PI * Time / 1.0f) * FVector(1, 0, 0);
			Component.InputWorldTransforms[0] = FTransform(InitialTransforms[0].GetTranslation() + Offset0);
			Component.InputWorldTransforms[1] = FTransform(InitialTransforms[1].GetTranslation() + Offset0);

			Component.Tick(Dt);

			Time += Dt;

			// Kinematic body output pose should match the input pose
			EXPECT_NEAR(Component.OutputWorldTransforms[0].GetTranslation().X, Component.InputWorldTransforms[0].GetTranslation().X, KINDA_SMALL_NUMBER);
			EXPECT_NEAR(Component.OutputWorldTransforms[0].GetTranslation().Y, Component.InputWorldTransforms[0].GetTranslation().Y, KINDA_SMALL_NUMBER);
			EXPECT_NEAR(Component.OutputWorldTransforms[0].GetTranslation().Z, Component.InputWorldTransforms[0].GetTranslation().Z, KINDA_SMALL_NUMBER);

			// Dynamic body should swing around the kinematic body at fixed distance
			const Chaos::FReal Distance = (Component.OutputWorldTransforms[1].GetTranslation() - Component.OutputWorldTransforms[0].GetTranslation()).Size();
			EXPECT_NEAR(Distance, InitialDistance, (FReal)3.0);
		}
	}
}
