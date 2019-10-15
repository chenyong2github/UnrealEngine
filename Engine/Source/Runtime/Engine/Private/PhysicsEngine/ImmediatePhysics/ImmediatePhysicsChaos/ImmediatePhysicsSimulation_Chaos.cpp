// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsSimulation_Chaos.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsActorHandle_Chaos.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsJointHandle_Chaos.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsStats.h"

#include "Chaos/ChaosDebugDraw.h"
#include "Chaos/DebugDrawQueue.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDCollisionConstraint.h"
#include "Chaos/PBDJointConstraints.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "ChaosLog.h"

#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/ConstraintInstance.h"

// @todo(ccaulfield): remove when finished
float ChaosImmediate_Evolution_DeltaTime = 0.03f;
int32 ChaosImmediate_Evolution_Iterations = 10;
int32 ChaosImmediate_Collision_Enabled = 1;
int32 ChaosImmediate_Collision_ApplyEnabled = 1;
int32 ChaosImmediate_Collision_PushOutIterations = 5;
int32 ChaosImmediate_Collision_PushOutPairIterations = 2;
float ChaosImmediate_Collision_Thickness = 0;

FAutoConsoleVariableRef CVarChaosImmPhysDeltaTime(TEXT("p.Chaos.ImmPhys.DeltaTime"), ChaosImmediate_Evolution_DeltaTime, TEXT("Override chaos immediate physics delta time if non-zero"));
FAutoConsoleVariableRef CVarChaosImmPhysIterations(TEXT("p.Chaos.ImmPhys.Iterations"), ChaosImmediate_Evolution_Iterations, TEXT("Number of constraint solver loops in immediate physics"));
FAutoConsoleVariableRef CVarImmediatePhysicsDisableCollisions(TEXT("p.Chaos.ImmPhys.EnableCollisions"), ChaosImmediate_Collision_Enabled, TEXT("Enable/Disable collisions in Immediate Physics."));
FAutoConsoleVariableRef CVarCollisionDisableApply(TEXT("p.Chaos.ImmPhys.CollisionEnableApply"), ChaosImmediate_Collision_ApplyEnabled, TEXT("Enable/Disable the Apply() (velocity, friction, restitution) method for collisions"));
FAutoConsoleVariableRef CVarCollisionPushOutIterations(TEXT("p.Chaos.ImmPhys.CollisionPushOutIterations"), ChaosImmediate_Collision_PushOutIterations, TEXT("Set the ApplyPushOut() (position correction) iteration count (0 to disable)"));
FAutoConsoleVariableRef CVarCollisionPushOutPairIterations(TEXT("p.Chaos.ImmPhys.CollisionPushOutPairIterations"), ChaosImmediate_Collision_PushOutPairIterations, TEXT("Set the ApplyPushOut() internal pair interations (position correction) iteration count (0 to disable)"));
FAutoConsoleVariableRef CVarChaosImmPhysThickness(TEXT("p.Chaos.ImmPhys.CollisionThickness"), ChaosImmediate_Collision_Thickness, TEXT("ChaosImmediateThickness"));

float ChaosImmediate_Joint_SwingTwistAngleTolerance = 1.0e-6f;
int32 ChaosImmediate_Joint_EnableLinearLimits = 1;
int32 ChaosImmediate_Joint_EnableTwistLimits = 1;
int32 ChaosImmediate_Joint_EnableSwingLimits = 1;
int32 ChaosImmediate_Joint_EnableDrives = 1;
int32 ChaosImmediate_Joint_FreezeIterations = 0;
int32 ChaosImmediate_Joint_FrozenIterations = 1;
float ChaosImmediate_Joint_PBDDriveStiffness = 0.0f;
float ChaosImmediate_Joint_PBDMinParentMassRatio = 0.5f;
float ChaosImmediate_Joint_PBDMaxInertiaRatio = 5.0f;
FAutoConsoleVariableRef CVarSwingTwistAngleTolerance(TEXT("p.Chaos.ImmPhys.Joint.SwingTwistAngleTolerance"), ChaosImmediate_Joint_SwingTwistAngleTolerance, TEXT("SwingTwistAngleTolerance."));
FAutoConsoleVariableRef CVarEnableLinearLimits(TEXT("p.Chaos.ImmPhys.Joint.EnableLinearLimits"), ChaosImmediate_Joint_EnableLinearLimits, TEXT("EnableLinearLimits."));
FAutoConsoleVariableRef CVarEnableTwistLimits(TEXT("p.Chaos.ImmPhys.Joint.EnableTwistLimits"), ChaosImmediate_Joint_EnableTwistLimits, TEXT("EnableTwistLimits."));
FAutoConsoleVariableRef CVarEnableSwingLimits(TEXT("p.Chaos.ImmPhys.Joint.EnableSwingLimits"), ChaosImmediate_Joint_EnableSwingLimits, TEXT("EnableSwingLimits."));
FAutoConsoleVariableRef CVarEnableDrives(TEXT("p.Chaos.ImmPhys.Joint.EnableDrives"), ChaosImmediate_Joint_EnableDrives, TEXT("EnableDrives."));
FAutoConsoleVariableRef CVarFreezeIterations(TEXT("p.Chaos.ImmPhys.Joint.FreezeIterations"), ChaosImmediate_Joint_FreezeIterations, TEXT("FreezeIterations."));
FAutoConsoleVariableRef CVarFrozenIterations(TEXT("p.Chaos.ImmPhys.Joint.FrozenIterations"), ChaosImmediate_Joint_FrozenIterations, TEXT("FrozenIterations."));
FAutoConsoleVariableRef CVarDriveStiffness(TEXT("p.Chaos.ImmPhys.Joint.PBDDriveStiffness"), ChaosImmediate_Joint_PBDDriveStiffness, TEXT("6Dof joint drive stiffness override (if > 0)."));
FAutoConsoleVariableRef CVarMinParentMassRatio(TEXT("p.Chaos.ImmPhys.Joint.MinParentMassRatio"), ChaosImmediate_Joint_PBDMinParentMassRatio, TEXT("6Dof joint PBDMinParentMassRatio (if > 0)"));
FAutoConsoleVariableRef CVarMaxInertiaRatio(TEXT("p.Chaos.ImmPhys.Joint.MaxInertiaRatio"), ChaosImmediate_Joint_PBDMaxInertiaRatio, TEXT("6Dof joint PBDMaxInertiaRatio (if > 0)"));

// DebugDraw CVars
#if UE_BUILD_DEBUG
int32 ChaosImmediate_DebugDrawParticles = 1;
int32 ChaosImmediate_DebugDrawShapes = 1;
int32 ChaosImmediate_DebugDrawCollisions = 3;
int32 ChaosImmediate_DebugDrawJoints = 1;
#else
int32 ChaosImmediate_DebugDrawParticles = 0;
int32 ChaosImmediate_DebugDrawShapes = 0;
int32 ChaosImmediate_DebugDrawCollisions = 0;
int32 ChaosImmediate_DebugDrawJoints = 0;
#endif

FAutoConsoleVariableRef CVarDebugDrawParticles(TEXT("p.Chaos.ImmPhys.DebugDrawParticles"), ChaosImmediate_DebugDrawParticles, TEXT("Draw Particle Transforms (0 = never; 1 = end of frame; 2 = begin and end of frame; 3 = post-integate, post-apply and post-applypushout;)."));
FAutoConsoleVariableRef CVarDebugDrawShapes(TEXT("p.Chaos.ImmPhys.DebugDrawShapes"), ChaosImmediate_DebugDrawShapes, TEXT("Draw Shapes (0 = never; 1 = end of frame; 2 = begin and end of frame; 3 = post-integate, post-apply and post-applypushout;"));
FAutoConsoleVariableRef CVarDebugDrawCollisions(TEXT("p.Chaos.ImmPhys.DebugDrawCollisions"), ChaosImmediate_DebugDrawCollisions, TEXT("Draw Collisions (0 = never; 1 = end of frame; 2 = begin and end of frame; 3 = post-integate, post-apply and post-applypushout;)"));
FAutoConsoleVariableRef CVarDebugDrawJoints(TEXT("p.Chaos.ImmPhys.DebugDrawJoints"), ChaosImmediate_DebugDrawJoints, TEXT("Draw Joints. (0 = never; 1 = end of frame; 2 = begin and end of frame; 3 = post-integate, post-apply and post-applypushout; 4 = each Apply step)."));

namespace ImmediatePhysics_Chaos
{
	template<typename T, int d>
	bool ShouldIgnoreCollisionConstraint(
		const Chaos::TGeometryParticleHandle<T, d>* ParticleA,
		const Chaos::TGeometryParticleHandle<T, d>* ParticleB,
		const TMap<const Chaos::TGeometryParticleHandle<T, d>*, TSet<const Chaos::TGeometryParticleHandle<T, d>*>>& IgnoreSetMap)
	{
		using namespace Chaos;

		if (!ChaosImmediate_Collision_Enabled)
		{
			return true;
		}
		if (const TSet<const TGeometryParticleHandle<T, d>*>* IgnoreSet = IgnoreSetMap.Find(ParticleA))
		{
			return IgnoreSet->Contains(ParticleB);
		}
		if (const TSet<const TGeometryParticleHandle<T, d>*>* IgnoreSet = IgnoreSetMap.Find(ParticleB))
		{
			return IgnoreSet->Contains(ParticleA);
		}
		return false;
	}

	//
	//
	//

	FSimulation::FSimulation()
		: NumActiveActorHandles(0)
		, SimulationSpaceTransform(FTransform::Identity)
	{
		using namespace Chaos;

		Particles = MakeUnique<TPBDRigidsSOAs<FReal, Dimensions>>();
		Joints = MakeUnique<TPBDJointConstraints<FReal, Dimensions>>(TPBDJointSolverSettings<FReal, Dimensions>());
		JointsRule = MakeUnique<TPBDConstraintIslandRule<TPBDJointConstraints<FReal, Dimensions>, FReal, Dimensions>>(*Joints);
		Evolution = MakeUnique<TPBDRigidsEvolutionGBF<FReal, Dimensions>>(*Particles.Get(), 1);
		TPBDCollisionConstraint<FReal, Dimensions>& Collisions = Evolution->GetCollisionConstraints();

		Evolution->AddConstraintRule(JointsRule.Get());

		// Filter collisions after detection
		// @todo(ccaulfield): Eventually we will build lists of potentially colliding pairs and won't need this
		Collisions.SetPostComputeCallback(
			[this]()
			{
				Evolution->GetCollisionConstraints().ApplyCollisionModifier(
					[this](TRigidBodyContactConstraint<float, 3>& Constraint)
					{
						if (ShouldIgnoreCollisionConstraint(Constraint.Particle, Constraint.Levelset, IgnoreCollisionParticlePairTable))
						{
							return ECollisionModifierResult::Disabled;
						}
						return ECollisionModifierResult::Unchanged;
					});
			});
		
#if CHAOS_DEBUG_DRAW
		Evolution->SetPostIntegrateCallback(
			[this]()
			{
				// Dynamic only - Kinematics get drawn once at the end of frame
				//DebugDrawIslandParticles(Island, 3, 3, 0.3f, false, true);
				//DebugDrawIslandConstraints(Island, 3, 3, 0.3f);
			});
		Evolution->SetPostApplyCallback(
			[this](int32 Island)
			{
				// Dynamic only - Kinematics get drawn once at the end of frame
				DebugDrawIslandParticles(Island, 3, 3, 0.3f, false, true);
				DebugDrawIslandConstraints(Island, 3, 3, 0.3f);
			});
		Evolution->SetPostApplyPushOutCallback(
			[this](int32 Island)
			{
				// Dynamic only - Kinematics get drawn once at the end of frame
				DebugDrawIslandParticles(Island, 3, 3, 1.0f, false, true);
				DebugDrawIslandConstraints(Island, 3, 3, 1.0f);
			});
		Collisions.SetPostApplyCallback(
			[this](const float Dt, const TArray<TPBDCollisionConstraintHandle<float, 3>*>& InConstraintHandles)
			{
				if (ChaosImmediate_DebugDrawCollisions == 4)
				{
					DebugDraw::DrawCollisions(SimulationSpaceTransform, InConstraintHandles, 0.3f);
				}
			});
		Collisions.SetPostApplyPushOutCallback(
			[this](const float Dt, const TArray<TPBDCollisionConstraintHandle<float, 3>*>& InConstraintHandles, bool bRequiresAnotherIteration)
			{
				if (ChaosImmediate_DebugDrawCollisions == 4)
				{
					DebugDraw::DrawCollisions(SimulationSpaceTransform, InConstraintHandles, 0.6f);
				}
			});
		Joints->SetPreApplyCallback(
			[this](const float Dt, const TArray<TPBDJointConstraintHandle<float, 3>*>& InConstraintHandles)
			{
				if (ChaosImmediate_DebugDrawJoints == 4)
				{
					DebugDraw::DrawJointConstraints(SimulationSpaceTransform, InConstraintHandles, 0.3f);
				}
			});
		Joints->SetPostApplyCallback(
			[this](const float Dt, const TArray<TPBDJointConstraintHandle<float, 3>*>& InConstraintHandles)
			{
				if (ChaosImmediate_DebugDrawJoints == 4)
				{
					DebugDraw::DrawJointConstraints(SimulationSpaceTransform, InConstraintHandles, 0.6f);
				}
			});
#endif
	}

	FSimulation::~FSimulation()
	{
		using namespace Chaos;

		Evolution->GetCollisionConstraints().ClearPostComputeCallback();
		Evolution->GetCollisionConstraints().ClearPostApplyCallback();
		Evolution->GetCollisionConstraints().ClearPostApplyPushOutCallback();

		//Evolution->RemoveConstraintRule(JointsRule.Get());

		for (FActorHandle* ActorHandle : ActorHandles)
		{
			delete ActorHandle;
		}
		ActorHandles.Empty();

		for (FJointHandle* JointHandle : JointHandles)
		{
			delete JointHandle;
		}
		JointHandles.Empty();

		Evolution.Reset();
		JointsRule.Reset();
		Joints.Reset();
		Particles.Reset();
	}

	FActorHandle* FSimulation::CreateStaticActor(FBodyInstance* BodyInstance)
	{
		return CreateActor(EActorType::StaticActor, BodyInstance, BodyInstance->GetUnrealWorldTransform());
	}

	FActorHandle* FSimulation::CreateKinematicActor(FBodyInstance* BodyInstance, const FTransform& Transform)
	{
		return CreateActor(EActorType::KinematicActor, BodyInstance, Transform);
	}

	FActorHandle* FSimulation::CreateDynamicActor(FBodyInstance* BodyInstance, const FTransform& Transform)
	{
		return CreateActor(EActorType::DynamicActor, BodyInstance, Transform);
	}

	FActorHandle* FSimulation::CreateActor(EActorType ActorType, FBodyInstance* BodyInstance, const FTransform& Transform)
	{
		FActorHandle* ActorHandle = new FActorHandle(Evolution.Get(), ActorType, BodyInstance, Transform);
		int ActorIndex = ActorHandles.Add(ActorHandle);
		return ActorHandle;
	}

	void FSimulation::DestroyActor(FActorHandle* ActorHandle)
	{
		// @todo(ccaulfield): FActorHandle could remember its index to optimize this
		ActorHandles.Remove(ActorHandle);
		delete ActorHandle;
	}

	FJointHandle* FSimulation::CreateJoint(FConstraintInstance* ConstraintInstance, FActorHandle* Body1, FActorHandle* Body2)
	{
		FJointHandle* JointHandle = new FJointHandle(Joints.Get(), ConstraintInstance, Body1, Body2);
		JointHandles.Add(JointHandle);
		return JointHandle;
	}

	void FSimulation::DestroyJoint(FJointHandle* JointHandle)
	{
		// @todo(ccaulfield): FJointHandle could remember its index to optimize this
		JointHandles.Remove(JointHandle);
		delete JointHandle;
	}

	void FSimulation::SetNumActiveBodies(int32 InNumActiveActorHandles)
	{
		if (InNumActiveActorHandles < NumActiveActorHandles)
		{
			for (int ActorHandleIndex = InNumActiveActorHandles; ActorHandleIndex < NumActiveActorHandles; ++ActorHandleIndex)
			{
				GetActorHandle(ActorHandleIndex)->SetEnabled(false);
			}
		}
		else if (InNumActiveActorHandles > NumActiveActorHandles)
		{
			check(InNumActiveActorHandles <= ActorHandles.Num());
			for (int ActorHandleIndex = NumActiveActorHandles; ActorHandleIndex < InNumActiveActorHandles; ++ActorHandleIndex)
			{
				GetActorHandle(ActorHandleIndex)->SetEnabled(true);
			}
		}
	
		NumActiveActorHandles = InNumActiveActorHandles;
	}

	void FSimulation::SetIgnoreCollisionPairTable(const TArray<FIgnorePair>& InIgnoreCollisionPairTable)
	{
		using namespace Chaos;

		IgnoreCollisionParticlePairTable.Reset();
		for (const FIgnorePair& IgnorePair : InIgnoreCollisionPairTable)
		{
			if(!IgnorePair.A || !IgnorePair.B)
			{
				continue;
			}

			const TGeometryParticleHandle<FReal, Dimensions>* ParticleA = IgnorePair.A->GetParticle();
			const TGeometryParticleHandle<FReal, Dimensions>* ParticleB = IgnorePair.B->GetParticle();

			IgnoreCollisionParticlePairTable.FindOrAdd(ParticleA).Add(ParticleB); 
			IgnoreCollisionParticlePairTable.FindOrAdd(ParticleB).Add(ParticleA);
		}
	}

	void FSimulation::SetIgnoreCollisionActors(const TArray<FActorHandle*>& InIgnoreCollisionActors)
	{
		using namespace Chaos;

		for (FActorHandle* ActorHandle : InIgnoreCollisionActors)
		{
			TPBDRigidParticleHandle<FReal, Dimensions>* Particle = ActorHandle->GetParticle()->AsDynamic();
			if (Particle != nullptr)
			{
				Particle->SetCollisionGroup(INDEX_NONE);
			}
		}
	}

	void FSimulation::ConditionConstraints()
	{
		// Assign levels to actors based on connection-distance to a non-dynamic actor
		// This is used to sort constraints and set parent/child relationship on the constrained particles in a constraint
		// @todo(ccaulfield): this should use the constraint graph and should only update when constraint connectivity changes or particles change type
		using namespace Chaos;

		TMap<FActorHandle*, TArray<FJointHandle*>> ActorJoints;
		TArray<FActorHandle*> ActorQueue;
		ActorQueue.Reserve(ActorHandles.Num());

		// Reset all actor levels
		for (FActorHandle* ActorHandle : ActorHandles)
		{
			ActorHandle->SetLevel(INDEX_NONE);
			ActorJoints.Emplace(ActorHandle);

			if (!ActorHandle->GetParticle()->AsDynamic())
			{
				ActorHandle->SetLevel(0);
				ActorQueue.Add(ActorHandle);
			}
		}

		// Build a list of joints per actor
		for (FJointHandle* JointHandle : JointHandles)
		{
			TVector<FActorHandle*, 2> JointActorHandles = JointHandle->GetActorHandles();
			ActorJoints[JointActorHandles[0]].Add(JointHandle);
			ActorJoints[JointActorHandles[1]].Add(JointHandle);
		}

		// Breadth-first assign level
		for (int32 ActorQueueIndex = 0; ActorQueueIndex < ActorQueue.Num(); ++ActorQueueIndex)
		{
			FActorHandle* ActorHandle = ActorQueue[ActorQueueIndex];
			for (FJointHandle* JointHandle : ActorJoints[ActorHandle])
			{
				TVector<FActorHandle*, 2> JointActorHandles = JointHandle->GetActorHandles();
				if (JointActorHandles[0]->GetLevel() == INDEX_NONE)
				{
					JointActorHandles[0]->SetLevel(ActorHandle->GetLevel() + 1);
					ActorQueue.Add(JointActorHandles[0]);
				}
				if (JointActorHandles[1]->GetLevel() == INDEX_NONE)
				{
					JointActorHandles[1]->SetLevel(ActorHandle->GetLevel() + 1);
					ActorQueue.Add(JointActorHandles[1]);
				}
			}
		}

		// Update constraint levels
		for (FJointHandle* JointHandle : JointHandles)
		{
			JointHandle->UpdateLevels();
		}
	}

	DECLARE_CYCLE_STAT(TEXT("FSimulation::Simulate_Chaos"), STAT_ImmediateSimulate_Chaos, STATGROUP_ImmediatePhysics);

	void FSimulation::Simulate(float DeltaTime, const FVector& InGravity)
	{
		SCOPE_CYCLE_COUNTER(STAT_ImmediateSimulate_Chaos);
		using namespace Chaos;

		// TEMP: overrides
		{
			if (ChaosImmediate_Evolution_DeltaTime > 0)
			{
				DeltaTime = ChaosImmediate_Evolution_DeltaTime;
			}
			UE_LOG(LogChaosJoint, Verbose, TEXT("Simulate dt = %f"), DeltaTime);

			// Overrides
			TPBDJointSolverSettings<float, 3> JointsSettings = Joints->GetSettings();
			JointsSettings.SwingTwistAngleTolerance = ChaosImmediate_Joint_SwingTwistAngleTolerance;
			JointsSettings.PBDMinParentMassRatio = ChaosImmediate_Joint_PBDMinParentMassRatio;
			JointsSettings.PBDMaxInertiaRatio = ChaosImmediate_Joint_PBDMaxInertiaRatio;
			JointsSettings.FreezeIterations = ChaosImmediate_Joint_FreezeIterations;
			JointsSettings.FrozenIterations = ChaosImmediate_Joint_FrozenIterations;
			JointsSettings.bEnableLinearLimits = ChaosImmediate_Joint_EnableLinearLimits != 0;
			JointsSettings.bEnableTwistLimits = ChaosImmediate_Joint_EnableTwistLimits != 0;
			JointsSettings.bEnableSwingLimits = ChaosImmediate_Joint_EnableSwingLimits != 0;
			JointsSettings.bEnableDrives = ChaosImmediate_Joint_EnableDrives != 0;
			JointsSettings.PBDDriveStiffness = ChaosImmediate_Joint_PBDDriveStiffness;
			Joints->SetSettings(JointsSettings);

			Evolution->SetNumIterations(ChaosImmediate_Evolution_Iterations);

			Evolution->GetCollisionConstraints().SetThickness(ChaosImmediate_Collision_Thickness);
			Evolution->GetCollisionConstraints().SetVelocitySolveEnabled(ChaosImmediate_Collision_ApplyEnabled != 0);
			Evolution->GetCollisionConstraints().SetPushOutPairIterations(ChaosImmediate_Collision_PushOutPairIterations);
			Evolution->GetCollisionConstraintsRule().SetPushOutIterations(ChaosImmediate_Collision_PushOutIterations);
		
			// TEMP until we can remove constraints again, or I add broad-phase filtering. (FilterCollisionConstraints will crash since the persistent collision changes)
			Evolution->GetCollisionConstraints().SetCollisionsEnabled(ChaosImmediate_Collision_Enabled != 0);
		}

		DebugDrawParticles(2, 2, 0.7f, true, true);
		DebugDrawConstraints(2, 2, 0.7f);

		ConditionConstraints();

		Evolution->GetGravityForces().SetAcceleration(InGravity);
		
		Evolution->AdvanceOneTimeStep(DeltaTime);
		
		Evolution->EndFrame(DeltaTime);

		DebugDrawParticles(1, 2, 1.0f, true, true);
		DebugDrawConstraints(1, 2, 1.0f);
		DebugDrawParticles(3, 3, 1.0f, true, false);	// Kinematics only
	}

	void FSimulation::DebugDrawParticles(const int32 MinDebugLevel, const int32 MaxDebugLevel, const float ColorScale, bool bDrawKinematic, bool bDrawDynamic)
	{
#if CHAOS_DEBUG_DRAW
		using namespace Chaos;
		if (FDebugDrawQueue::IsDebugDrawingEnabled())
		{
			if ((ChaosImmediate_DebugDrawParticles >= MinDebugLevel) && (ChaosImmediate_DebugDrawParticles <= MaxDebugLevel))
			{
				DebugDraw::DrawParticleTransforms(SimulationSpaceTransform, Evolution->GetParticles().GetAllParticlesView(), ColorScale, bDrawKinematic, bDrawDynamic);
			}
			if ((ChaosImmediate_DebugDrawShapes >= MinDebugLevel) && (ChaosImmediate_DebugDrawShapes <= MaxDebugLevel))
			{
				DebugDraw::DrawParticleShapes(SimulationSpaceTransform, Evolution->GetParticles().GetAllParticlesView(), ColorScale, bDrawKinematic, bDrawDynamic);
			}
		}
#endif
	}

	void FSimulation::DebugDrawConstraints(const int32 MinDebugLevel, const int32 MaxDebugLevel, const float ColorScale)
	{
#if CHAOS_DEBUG_DRAW
		using namespace Chaos;
		if (FDebugDrawQueue::IsDebugDrawingEnabled())
		{
			if ((ChaosImmediate_DebugDrawCollisions >= MinDebugLevel) && (ChaosImmediate_DebugDrawCollisions <= MaxDebugLevel))
			{
				DebugDraw::DrawCollisions(SimulationSpaceTransform, Evolution->GetCollisionConstraints(), ColorScale);
			}
			if ((ChaosImmediate_DebugDrawJoints >= MinDebugLevel) && (ChaosImmediate_DebugDrawJoints <= MaxDebugLevel))
			{
				DebugDraw::DrawJointConstraints(SimulationSpaceTransform, *Joints, ColorScale);
			}
		}
#endif
	}

	void FSimulation::DebugDrawIslandParticles(const int32 Island, const int32 MinDebugLevel, const int32 MaxDebugLevel, const float ColorScale, bool bDrawKinematic, bool bDrawDynamic)
	{
#if CHAOS_DEBUG_DRAW
		using namespace Chaos;
		if (FDebugDrawQueue::IsDebugDrawingEnabled())
		{
			if ((ChaosImmediate_DebugDrawParticles >= MinDebugLevel) && (ChaosImmediate_DebugDrawParticles <= MaxDebugLevel))
			{
				DebugDraw::DrawParticleTransforms(SimulationSpaceTransform, Evolution->GetIslandParticles(Island), ColorScale, bDrawKinematic, bDrawDynamic);
			}
			if ((ChaosImmediate_DebugDrawShapes >= MinDebugLevel) && (ChaosImmediate_DebugDrawShapes <= MaxDebugLevel))
			{
				DebugDraw::DrawParticleShapes(SimulationSpaceTransform, Evolution->GetIslandParticles(Island), ColorScale, bDrawKinematic, bDrawDynamic);
			}
		}
#endif
	}

	void FSimulation::DebugDrawIslandConstraints(const int32 Island, const int32 MinDebugLevel, const int32 MaxDebugLevel, const float ColorScale)
	{
#if CHAOS_DEBUG_DRAW
		using namespace Chaos;
		if (FDebugDrawQueue::IsDebugDrawingEnabled())
		{
			if ((ChaosImmediate_DebugDrawCollisions >= MinDebugLevel) && (ChaosImmediate_DebugDrawCollisions <= MaxDebugLevel))
			{
				Evolution->GetCollisionConstraintsRule().VisitIslandConstraints(Island,
					[this, ColorScale](const TArray<TPBDCollisionConstraintHandle<float, 3>*>& ConstraintHandles)
					{
						DebugDraw::DrawCollisions(SimulationSpaceTransform, ConstraintHandles, ColorScale);
					});
			}
			if ((ChaosImmediate_DebugDrawJoints >= MinDebugLevel) && (ChaosImmediate_DebugDrawJoints <= MaxDebugLevel))
			{
				JointsRule->VisitIslandConstraints(Island,
					[this, ColorScale](const TArray<TPBDJointConstraintHandle<float, 3>*>& ConstraintHandles)
					{
						DebugDraw::DrawJointConstraints(SimulationSpaceTransform, *Joints, ColorScale);
					});
			}
		}
#endif
	}
}
