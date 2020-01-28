// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsSimulation_Chaos.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsActorHandle_Chaos.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsJointHandle_Chaos.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsStats.h"

#include "Chaos/ChaosDebugDraw.h"
#include "Chaos/DebugDrawQueue.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/PBDJointConstraints.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "ChaosLog.h"

#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/ConstraintInstance.h"

//#pragma optimize("", off)

DECLARE_CYCLE_STAT(TEXT("FSimulation::Simulate_Chaos"), STAT_ImmediateSimulate_Chaos, STATGROUP_ImmediatePhysics);

//////////////////////////////////////////////////////////////////////////
// @todo(ccaulfield): remove when finished
//
float ChaosImmediate_Evolution_StepTime = 0.0f;
int32 ChaosImmediate_Evolution_NumSteps = 0;
float ChaosImmediate_Evolution_InitialStepTime = 0.033f;
int32 ChaosImmediate_Evolution_DeltaTimeCount = 100;
int32 ChaosImmediate_Evolution_Iterations = -1;
int32 ChaosImmediate_Evolution_PushOutIterations = -1;
FAutoConsoleVariableRef CVarChaosImmPhysStepTime(TEXT("p.Chaos.ImmPhys.StepTime"), ChaosImmediate_Evolution_StepTime, TEXT("Override step time (if not zero)"));
FAutoConsoleVariableRef CVarChaosImmPhysNumSteps(TEXT("p.Chaos.ImmPhys.NumSteps"), ChaosImmediate_Evolution_NumSteps, TEXT("Override num steps (if not zero)"));
FAutoConsoleVariableRef CVarChaosImmPhysInitialStepTime(TEXT("p.Chaos.ImmPhys.InitialStepTime"), ChaosImmediate_Evolution_InitialStepTime, TEXT("Initial step time (then calculated from rolling average)"));
FAutoConsoleVariableRef CVarChaosImmPhysDeltaTimeCount(TEXT("p.Chaos.ImmPhys.DeltaTimeCount"), ChaosImmediate_Evolution_DeltaTimeCount, TEXT("The number of ticks over which the moving average is calculated"));
FAutoConsoleVariableRef CVarChaosImmPhysIterations(TEXT("p.Chaos.ImmPhys.Iterations"), ChaosImmediate_Evolution_Iterations, TEXT("Override number of constraint solver loops in immediate physics (if >= 0)"));
FAutoConsoleVariableRef CVarChaosImmPhysPushOutIterations(TEXT("p.Chaos.ImmPhys.PushOutIterations"), ChaosImmediate_Evolution_PushOutIterations, TEXT("Override number of solver push-out loops (if >= 0)"));

int32 ChaosImmediate_Collision_Enabled = 1;
int32 ChaosImmediate_Collision_PairIterations = -1;
int32 ChaosImmediate_Collision_PushOutPairIterations = -1;
int32 ChaosImmediate_Collision_Priority = 1;
float ChaosImmediate_Collision_Thickness = 0;
FAutoConsoleVariableRef CVarChaosImmPhysCollisionDisable(TEXT("p.Chaos.ImmPhys.Collision.Enabled"), ChaosImmediate_Collision_Enabled, TEXT("Enable/Disable collisions in Immediate Physics."));
FAutoConsoleVariableRef CVarChaosImmPhysCollisionPairIterations(TEXT("p.Chaos.ImmPhys.Collision.PairIterations"), ChaosImmediate_Collision_PairIterations, TEXT("Override collision pair iterations (if >= 0)"));
FAutoConsoleVariableRef CVarChaosImmPhysCollisionPushOutPairIterations(TEXT("p.Chaos.ImmPhys.Collision.PushOutPairIterations"), ChaosImmediate_Collision_PushOutPairIterations, TEXT("Override collision push-out pair iterations (if >= 0)"));
FAutoConsoleVariableRef CVarChaosImmPhysCollisionPriority(TEXT("p.Chaos.ImmPhys.Collision.Priority"), ChaosImmediate_Collision_Priority, TEXT("Set the Collision constraint sort order (Joints have priority 0)"));
FAutoConsoleVariableRef CVarChaosImmPhysCollisionThickness(TEXT("p.Chaos.ImmPhys.Collision.Thickness"), ChaosImmediate_Collision_Thickness, TEXT("ChaosImmediateThickness"));

int32 ChaosImmediate_Joint_PairIterations = -1;
int32 ChaosImmediate_Joint_PushOutPairIterations = -1;
float ChaosImmediate_Joint_SwingTwistAngleTolerance = 1.0e-6f;
int32 ChaosImmediate_Joint_EnableTwistLimits = 1;
int32 ChaosImmediate_Joint_EnableSwingLimits = 1;
int32 ChaosImmediate_Joint_EnableDrives = 1;
float ChaosImmediate_Joint_LinearProjection = 0.0f;
float ChaosImmediate_Joint_AngularProjection = 0.0f;
float ChaosImmediate_Joint_Stiffness = 1.0f;
float ChaosImmediate_Joint_SoftLinearStiffness = 0.0f;
float ChaosImmediate_Joint_SoftTwistStiffness = 0.0f;
float ChaosImmediate_Joint_SoftTwistDamping = 0.0f;
float ChaosImmediate_Joint_SoftSwingStiffness = 0.0f;
float ChaosImmediate_Joint_SoftSwingDamping = 0.0f;
float ChaosImmediate_Joint_LinearDriveStiffness = 0.0f;
float ChaosImmediate_Joint_LinearDriveDamping = 0.0f;
float ChaosImmediate_Joint_AngularDriveStiffness = 0.0f;
float ChaosImmediate_Joint_AngularDriveDamping = 0.0f;
float ChaosImmediate_Joint_MinParentMassRatio = 0.2f;
float ChaosImmediate_Joint_MaxInertiaRatio = 5.0f;
float ChaosImmediate_Joint_AngularPositionCorrection = 1.0f;
FAutoConsoleVariableRef CVarChaosImmPhysJointPairIterations(TEXT("p.Chaos.ImmPhys.Joint.PairIterations"), ChaosImmediate_Joint_PairIterations, TEXT("Override joint pair iterations (if >= 0)"));
FAutoConsoleVariableRef CVarChaosImmPhysJointPushOutPairIterations(TEXT("p.Chaos.ImmPhys.Joint.PushOutPairIterations"), ChaosImmediate_Joint_PushOutPairIterations, TEXT("Override joint push-out pair iterations (if >= 0)"));
FAutoConsoleVariableRef CVarChaosImmPhysJointSwingTwistAngleTolerance(TEXT("p.Chaos.ImmPhys.Joint.SwingTwistAngleTolerance"), ChaosImmediate_Joint_SwingTwistAngleTolerance, TEXT("SwingTwistAngleTolerance."));
FAutoConsoleVariableRef CVarChaosImmPhysJointEnableTwistLimits(TEXT("p.Chaos.ImmPhys.Joint.EnableTwistLimits"), ChaosImmediate_Joint_EnableTwistLimits, TEXT("EnableTwistLimits."));
FAutoConsoleVariableRef CVarChaosImmPhysJointEnableSwingLimits(TEXT("p.Chaos.ImmPhys.Joint.EnableSwingLimits"), ChaosImmediate_Joint_EnableSwingLimits, TEXT("EnableSwingLimits."));
FAutoConsoleVariableRef CVarChaosImmPhysJointEnableDrives(TEXT("p.Chaos.ImmPhys.Joint.EnableDrives"), ChaosImmediate_Joint_EnableDrives, TEXT("EnableDrives."));
FAutoConsoleVariableRef CVarChaosImmPhysJointLinearProjection(TEXT("p.Chaos.ImmPhys.Joint.LinearProjection"), ChaosImmediate_Joint_LinearProjection, TEXT("6Dof joint projection amount override (if > 0)."));
FAutoConsoleVariableRef CVarChaosImmPhysJointAngularProjection(TEXT("p.Chaos.ImmPhys.Joint.AngularProjection"), ChaosImmediate_Joint_AngularProjection, TEXT("6Dof joint projection amount override (if > 0)."));
FAutoConsoleVariableRef CVarChaosImmPhysJointStiffness(TEXT("p.Chaos.ImmPhys.Joint.Stiffness"), ChaosImmediate_Joint_Stiffness, TEXT("6Dof joint stiffness override (if > 0)."));
FAutoConsoleVariableRef CVarChaosImmPhysJointSoftLinearStiffness(TEXT("p.Chaos.ImmPhys.Joint.SoftLinearStiffness"), ChaosImmediate_Joint_SoftLinearStiffness, TEXT("6Dof joint soft linear stiffness override (if > 0)."));
FAutoConsoleVariableRef CVarChaosImmPhysJointSoftTwistStiffness(TEXT("p.Chaos.ImmPhys.Joint.SoftTwistStiffness"), ChaosImmediate_Joint_SoftTwistStiffness, TEXT("6Dof joint SoftTwist stiffness override (if > 0)."));
FAutoConsoleVariableRef CVarChaosImmPhysJointSoftTwistDamping(TEXT("p.Chaos.ImmPhys.Joint.SoftTwistDamping"), ChaosImmediate_Joint_SoftTwistDamping, TEXT("6Dof joint SoftTwist damping override (if > 0)."));
FAutoConsoleVariableRef CVarChaosImmPhysJointSoftSwingStiffness(TEXT("p.Chaos.ImmPhys.Joint.SoftSwingStiffness"), ChaosImmediate_Joint_SoftSwingStiffness, TEXT("6Dof joint SoftSwing stiffness override (if > 0)."));
FAutoConsoleVariableRef CVarChaosImmPhysJointSoftSwingDamping(TEXT("p.Chaos.ImmPhys.Joint.SoftSwingDamping"), ChaosImmediate_Joint_SoftSwingDamping, TEXT("6Dof joint SoftSwing damping override (if > 0)."));
FAutoConsoleVariableRef CVarChaosImmPhysJointLinearDriveStiffness(TEXT("p.Chaos.ImmPhys.Joint.LinearDriveStiffness"), ChaosImmediate_Joint_LinearDriveStiffness, TEXT("6Dof joint drive stiffness override (if > 0)."));
FAutoConsoleVariableRef CVarChaosImmPhysJointLinearDriveDamping(TEXT("p.Chaos.ImmPhys.Joint.LinearDriveDamping"), ChaosImmediate_Joint_LinearDriveDamping, TEXT("6Dof joint drive damping override (if > 0)."));
FAutoConsoleVariableRef CVarChaosImmPhysJointAngularDriveStiffness(TEXT("p.Chaos.ImmPhys.Joint.AngularDriveStiffness"), ChaosImmediate_Joint_AngularDriveStiffness, TEXT("6Dof joint drive stiffness override (if > 0)."));
FAutoConsoleVariableRef CVarChaosImmPhysJointAngularDriveDamping(TEXT("p.Chaos.ImmPhys.Joint.AngularDriveDamping"), ChaosImmediate_Joint_AngularDriveDamping, TEXT("6Dof joint drive damping override (if > 0)."));
FAutoConsoleVariableRef CVarChaosImmPhysJointMinParentMassRatio(TEXT("p.Chaos.ImmPhys.Joint.MinParentMassRatio"), ChaosImmediate_Joint_MinParentMassRatio, TEXT("6Dof joint MinParentMassRatio (if > 0)"));
FAutoConsoleVariableRef CVarChaosImmPhysJointMaxInertiaRatio(TEXT("p.Chaos.ImmPhys.Joint.MaxInertiaRatio"), ChaosImmediate_Joint_MaxInertiaRatio, TEXT("6Dof joint MaxInertiaRatio (if > 0)"));
FAutoConsoleVariableRef CVarChaosImmPhysJointAngularPositionCorrection(TEXT("p.Chaos.ImmPhys.Joint.AngularPositionCorrection"), ChaosImmediate_Joint_AngularPositionCorrection, TEXT("6Dof joint post-rotation constraint position correction amount [0-1]"));

//
// end remove when finished
//
//////////////////////////////////////////////////////////////////////////


// DebugDraw CVars
#if UE_BUILD_DEBUG
int32 ChaosImmediate_DebugDrawParticles = 0;
int32 ChaosImmediate_DebugDrawShapes = 1;
int32 ChaosImmediate_DebugDrawBounds = 0;
int32 ChaosImmediate_DebugDrawCollisions = 0;
int32 ChaosImmediate_DebugDrawJoints = 1;
#else
int32 ChaosImmediate_DebugDrawParticles = 0;
int32 ChaosImmediate_DebugDrawShapes = 0;
int32 ChaosImmediate_DebugDrawBounds = 0;
int32 ChaosImmediate_DebugDrawCollisions = 0;
int32 ChaosImmediate_DebugDrawJoints = 0;
#endif

#if CHAOS_DEBUG_DRAW
int32 ChaosImmediate_DebugDrawJointFeatures = (int32)Chaos::DebugDraw::EDebugDrawJointFeature::Default;
#else
int32 ChaosImmediate_DebugDrawJointFeatures = 0;
#endif

FAutoConsoleVariableRef CVarChaosImmPhysDebugDrawParticles(TEXT("p.Chaos.ImmPhys.DebugDrawParticles"), ChaosImmediate_DebugDrawParticles, TEXT("Draw Particle Transforms (0 = never; 1 = end of frame; 2 = begin and end of frame; 3 = post-integate, post-apply and post-applypushout;)."));
FAutoConsoleVariableRef CVarChaosImmPhysDebugDrawShapes(TEXT("p.Chaos.ImmPhys.DebugDrawShapes"), ChaosImmediate_DebugDrawShapes, TEXT("Draw Shapes (0 = never; 1 = end of frame; 2 = begin and end of frame; 3 = post-integate, post-apply and post-applypushout;"));
FAutoConsoleVariableRef CVarChaosImmPhysDebugDrawBounds(TEXT("p.Chaos.ImmPhys.DebugDrawBounds"), ChaosImmediate_DebugDrawBounds, TEXT("Draw Particle Bounds (0 = never; 1 = end of frame; 2 = begin and end of frame; 3 = post-integate, post-apply and post-applypushout;)."));
FAutoConsoleVariableRef CVarChaosImmPhysDebugDrawCollisions(TEXT("p.Chaos.ImmPhys.DebugDrawCollisions"), ChaosImmediate_DebugDrawCollisions, TEXT("Draw Collisions (0 = never; 1 = end of frame; 2 = begin and end of frame; 3 = post-integate, post-apply and post-applypushout;)"));
FAutoConsoleVariableRef CVarChaosImmPhysDebugDrawJoints(TEXT("p.Chaos.ImmPhys.DebugDrawJoints"), ChaosImmediate_DebugDrawJoints, TEXT("Draw Joints. (0 = never; 1 = end of frame; 2 = begin and end of frame; 3 = post-integate, post-apply and post-applypushout; 4 = each Apply step)."));
FAutoConsoleVariableRef CVarChaosImmPhysDebugDrawJointFeatures(TEXT("p.Chaos.ImmPhys.DebugDrawJointFeatures"), ChaosImmediate_DebugDrawJointFeatures, TEXT("Joint features mask (see EDebugDrawJointFeature)."));

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
		: PotentiallyCollidingPairs()
		, CollidedParticles()
		, ParticleMaterials()
		, PerParticleMaterials()
		, Particles()
		, Joints()
		, Collisions(Particles, CollidedParticles, ParticleMaterials, 0, 0)
		, BroadPhase(PotentiallyCollidingPairs)
		, CollisionDetector(BroadPhase, Collisions)
		, JointsRule(0, Joints)
		, CollisionsRule(1, Collisions)
		, Evolution(Particles, CollisionDetector)
		, NumActiveDynamicActorHandles(0)
		, SimulationSpaceTransform(FTransform::Identity)
		, RollingAverageStepTime(ChaosImmediate_Evolution_InitialStepTime)
		, NumRollingAverageStepTimes(1)
		, MaxNumRollingAverageStepTimes(ChaosImmediate_Evolution_DeltaTimeCount)
	{
		using namespace Chaos;

		Particles.GetParticleHandles().AddArray(&ParticleMaterials);
		Particles.GetParticleHandles().AddArray(&PerParticleMaterials);

		Evolution.AddConstraintRule(&CollisionsRule);
		Evolution.AddConstraintRule(&JointsRule);

#if CHAOS_DEBUG_DRAW
		Evolution.SetPostIntegrateCallback(
			[this]()
			{
				DebugDrawDynamicParticles(3, 3, FColor(32, 32, 0));
				DebugDrawConstraints(3, 3, 0.3f);
			});
		Evolution.SetPostApplyCallback(
			[this]()
			{
				DebugDrawDynamicParticles(3, 3, FColor(128, 128, 0));
				DebugDrawConstraints(3, 3, 0.6f);
			});
		Evolution.SetPostApplyPushOutCallback(
			[this]()
			{
				DebugDrawDynamicParticles(3, 3, FColor(255, 255, 0));
				DebugDrawConstraints(3, 3, 1.0f);
			});
		Collisions.SetPostApplyCallback(
			[this](const float Dt, const TArray<TPBDCollisionConstraintHandle<float, 3>*>& InConstraintHandles)
			{
				if (ChaosImmediate_DebugDrawCollisions == 4)
				{
					DebugDraw::DrawCollisions(SimulationSpaceTransform, InConstraintHandles, 0.3f);
				}
				DebugDrawDynamicParticles(4, 4, FColor(128, 0, 0));
			});
		Collisions.SetPostApplyPushOutCallback(
			[this](const float Dt, const TArray<TPBDCollisionConstraintHandle<float, 3>*>& InConstraintHandles, bool bRequiresAnotherIteration)
			{
				if (ChaosImmediate_DebugDrawCollisions == 4)
				{
					DebugDraw::DrawCollisions(SimulationSpaceTransform, InConstraintHandles, 0.6f);
				}
			});
		Joints.SetPreApplyCallback(
			[this](const float Dt, const TArray<FPBDJointConstraintHandle*>& InConstraintHandles)
			{
				if (ChaosImmediate_DebugDrawJoints == 4)
				{
					DebugDraw::DrawJointConstraints(SimulationSpaceTransform, InConstraintHandles, 0.3f);
				}
			});
		Joints.SetPostApplyCallback(
			[this](const float Dt, const TArray<FPBDJointConstraintHandle*>& InConstraintHandles)
			{
				if (ChaosImmediate_DebugDrawJoints == 4)
				{
					DebugDraw::DrawJointConstraints(SimulationSpaceTransform, InConstraintHandles, 0.6f);
				}
				DebugDrawDynamicParticles(4, 4, FColor(0, 128, 0));
			});
		Joints.SetPostProjectCallback(
			[this](const float Dt, const TArray<FPBDJointConstraintHandle*>& InConstraintHandles)
			{
				if (ChaosImmediate_DebugDrawJoints == 4)
				{
					DebugDraw::DrawJointConstraints(SimulationSpaceTransform, InConstraintHandles, 0.6f);
				}
				DebugDrawDynamicParticles(4, 4, FColor(0, 0, 128));
			});
#endif
	}

	FSimulation::~FSimulation()
	{
		using namespace Chaos;

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
		using namespace Chaos;

		FActorHandle* ActorHandle = new FActorHandle(Particles, ActorType, BodyInstance, Transform);
		int ActorIndex = ActorHandles.Add(ActorHandle);

		// @todo(ccaulfield): shared materials
		TUniquePtr<FChaosPhysicsMaterial> Material = MakeUnique<FChaosPhysicsMaterial>();
		if (BodyInstance != nullptr)
		{
			UPhysicalMaterial* SimplePhysMat = BodyInstance->GetSimplePhysicalMaterial();
			if (SimplePhysMat != nullptr)
			{
				Material->Friction = SimplePhysMat->Friction;
				Material->Restitution = SimplePhysMat->Restitution;
			}
		}

		ParticleMaterials.Add(MakeSerializable(Material));
		PerParticleMaterials.Add(MoveTemp(Material));
		CollidedParticles.Add(false);

		return ActorHandle;
	}

	void FSimulation::DestroyActor(FActorHandle* ActorHandle)
	{
		// @todo(ccaulfield): FActorHandle could remember its index to optimize this
		int32 Index = ActorHandles.Remove(ActorHandle);
		delete ActorHandle;

		ParticleMaterials.RemoveAt(Index, 1);
		PerParticleMaterials.RemoveAt(Index, 1);
		CollidedParticles.RemoveAt(Index, 1);
	}

	FJointHandle* FSimulation::CreateJoint(FConstraintInstance* ConstraintInstance, FActorHandle* Body1, FActorHandle* Body2)
	{
		FJointHandle* JointHandle = new FJointHandle(&Joints, ConstraintInstance, Body1, Body2);
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
		if (InNumActiveActorHandles == NumActiveDynamicActorHandles)
		{
			return;
		}

		// @todo(ccaulfield): can be optimized, but I think we end up with kinematic at the start and statics at the end of the 
		// list - maybe split them up or put kinematics at the end (in a way that does not impact particle order).
		NumActiveDynamicActorHandles = 0;
		for (int32 ActorHandleIndex = 0; ActorHandleIndex < ActorHandles.Num(); ++ActorHandleIndex)
		{
			FActorHandle* Handle = GetActorHandle(ActorHandleIndex);
			if (Handle->GetIsKinematic())
			{
				continue;
			}

			if (NumActiveDynamicActorHandles < InNumActiveActorHandles)
			{
				Handle->SetEnabled(true);
				++NumActiveDynamicActorHandles;
			}
			else
			{
				Handle->SetEnabled(false);
			}
		}
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

		PotentiallyCollidingPairs.Empty();
		int NumActorHandles = ActorHandles.Num();
		for (int ActorHandleIndex0 = 0; ActorHandleIndex0 < NumActorHandles; ++ActorHandleIndex0)
		{
			FActorHandle* ActorHandle0 = ActorHandles[ActorHandleIndex0];
			TGeometryParticleHandle<FReal, Dimensions>* Particle0 = ActorHandle0->GetParticle();
			//TPBDRigidParticleHandle<FReal, Dimensions>* Rigid0 = Particle0->CastToRigidParticle();
			//if (Rigid0 && (Rigid0->ObjectState() == EObjectStateType::Dynamic))
			{
				for (int ActorHandleIndex1 = ActorHandleIndex0 + 1; ActorHandleIndex1 < NumActorHandles; ++ActorHandleIndex1)
				{
					FActorHandle* ActorHandle1 = ActorHandles[ActorHandleIndex1];
					TGeometryParticleHandle<FReal, Dimensions>* Particle1 = ActorHandle1->GetParticle();

					const TSet<const FParticleHandle*>* Particle0IgnoreSet = IgnoreCollisionParticlePairTable.Find(Particle0);
					bool bIgnoreActorHandlePair = (Particle0IgnoreSet != nullptr) && Particle0IgnoreSet->Contains(Particle1);
					if (!bIgnoreActorHandlePair)
					{
						PotentiallyCollidingPairs.Emplace(FParticlePair(Particle0, Particle1));
					}
				}
			}
		}

	}

	void FSimulation::SetIgnoreCollisionActors(const TArray<FActorHandle*>& InIgnoreCollisionActors)
	{
		using namespace Chaos;

		for (FActorHandle* ActorHandle : InIgnoreCollisionActors)
		{
			TPBDRigidParticleHandle<FReal, Dimensions>* Particle = ActorHandle->GetParticle()->CastToRigidParticle();
			if (Particle != nullptr && Particle->ObjectState() == EObjectStateType::Dynamic)
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

			if (ActorHandle->GetParticle()->ObjectState() != EObjectStateType::Dynamic)
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

	FReal FSimulation::UpdateStepTime(const FReal DeltaTime, const FReal MaxStepTime)
	{
		RollingAverageStepTime = RollingAverageStepTime + (DeltaTime - RollingAverageStepTime) / NumRollingAverageStepTimes;
		RollingAverageStepTime = FMath::Min(RollingAverageStepTime, MaxStepTime);
		NumRollingAverageStepTimes = FMath::Min(NumRollingAverageStepTimes + 1, MaxNumRollingAverageStepTimes);
		return RollingAverageStepTime;
	}

	void FSimulation::Simulate(float InDeltaTime, float MaxStepTime, int32 MaxSubSteps, const FVector& InGravity)
	{
		SCOPE_CYCLE_COUNTER(STAT_ImmediateSimulate_Chaos);
		using namespace Chaos;

		// Reject DeltaTime outliers
		const FReal DeltaTime = FMath::Min(InDeltaTime, MaxStepTime * MaxSubSteps);

		// Update rolling average step time - we want a smooth step time from frame-to-frame that is roughly the target frame rate.
		// @todo(ccaulfield): decouple sim and game delta times and simulate ahead. Add extrapolation of kinematic targets, and interpolation of physics results.
		FReal StepTime = UpdateStepTime(DeltaTime, MaxStepTime);

		// Calculate number of steps to run
		int32 NumSteps = FMath::Clamp(FMath::RoundToInt(DeltaTime / StepTime), 1, MaxSubSteps);

		// TEMP: overrides
		{
			if (ChaosImmediate_Evolution_StepTime > 0)
			{
				StepTime = ChaosImmediate_Evolution_StepTime;
			}
			if (ChaosImmediate_Evolution_NumSteps > 0)
			{
				NumSteps = ChaosImmediate_Evolution_NumSteps;
			}

			FPBDJointSolverSettings JointsSettings = Joints.GetSettings();
			JointsSettings.SwingTwistAngleTolerance = ChaosImmediate_Joint_SwingTwistAngleTolerance;
			JointsSettings.MinParentMassRatio = ChaosImmediate_Joint_MinParentMassRatio;
			JointsSettings.MaxInertiaRatio = ChaosImmediate_Joint_MaxInertiaRatio;
			JointsSettings.AngularConstraintPositionCorrection = ChaosImmediate_Joint_AngularPositionCorrection;
			JointsSettings.bEnableTwistLimits = ChaosImmediate_Joint_EnableTwistLimits != 0;
			JointsSettings.bEnableSwingLimits = ChaosImmediate_Joint_EnableSwingLimits != 0;
			JointsSettings.bEnableDrives = ChaosImmediate_Joint_EnableDrives != 0;
			JointsSettings.LinearProjection = ChaosImmediate_Joint_LinearProjection;
			JointsSettings.AngularProjection = ChaosImmediate_Joint_AngularProjection;
			JointsSettings.Stiffness = ChaosImmediate_Joint_Stiffness;
			JointsSettings.SoftLinearStiffness = ChaosImmediate_Joint_SoftLinearStiffness;
			JointsSettings.SoftTwistStiffness = ChaosImmediate_Joint_SoftTwistStiffness;
			JointsSettings.SoftTwistDamping = ChaosImmediate_Joint_SoftTwistDamping;
			JointsSettings.SoftSwingStiffness = ChaosImmediate_Joint_SoftSwingStiffness;
			JointsSettings.SoftSwingDamping = ChaosImmediate_Joint_SoftSwingDamping;
			JointsSettings.LinearDriveStiffness = ChaosImmediate_Joint_LinearDriveStiffness;
			JointsSettings.LinearDriveDamping = ChaosImmediate_Joint_LinearDriveDamping;
			JointsSettings.AngularDriveStiffness = ChaosImmediate_Joint_AngularDriveStiffness;
			JointsSettings.AngularDriveDamping = ChaosImmediate_Joint_AngularDriveDamping;
			Joints.SetSettings(JointsSettings);

			Collisions.SetThickness(ChaosImmediate_Collision_Thickness);
			Collisions.SetCollisionsEnabled(ChaosImmediate_Collision_Enabled != 0);
			CollisionsRule.SetPriority(ChaosImmediate_Collision_Priority);

			SetSolverIterations(
				ChaosImmediate_Evolution_Iterations,
				ChaosImmediate_Joint_PairIterations,
				ChaosImmediate_Collision_PairIterations,
				ChaosImmediate_Evolution_PushOutIterations,
				ChaosImmediate_Joint_PushOutPairIterations,
				ChaosImmediate_Collision_PushOutPairIterations);
		}
		UE_LOG(LogChaosJoint, Verbose, TEXT("Simulate Dt = %f Steps %d x %f"), DeltaTime, NumSteps, StepTime);

		DebugDrawKinematicParticles(2, 2, FColor(128, 0, 0));
		DebugDrawDynamicParticles(2, 2, FColor(192, 192, 0));
		DebugDrawConstraints(2, 2, 0.7f);

		ConditionConstraints();

		Evolution.SetGravity(InGravity);
		
		Evolution.Advance(StepTime, NumSteps);

		DebugDrawKinematicParticles(1, 4, FColor(128, 0, 0));
		DebugDrawDynamicParticles(1, 4, FColor(255, 255, 0));
		DebugDrawConstraints(1, 2, 1.0f);
	}

	void FSimulation::SetSolverIterations(const int32 SolverIts, const int32 JointIts, const int32 CollisionIts, const int32 SolverPushOutIts, const int32 JointPushOutIts, const int32 CollisionPushOutIts)
	{
		if (SolverIts >= 0)
		{
			Evolution.SetNumIterations(SolverIts);
		}
		if (SolverPushOutIts >= 0)
		{
			Evolution.SetNumPushOutIterations(SolverPushOutIts);
		}

		if (JointIts >= 0)
		{
			Joints.SetNumPairIterations(JointIts);
		}
		if (JointPushOutIts >= 0)
		{
			Joints.SetNumPushOutPairIterations(JointPushOutIts);
		}

		if (CollisionIts >= 0)
		{
			Collisions.SetPairIterations(CollisionIts);
		}
		if (CollisionPushOutIts >= 0)
		{
			Collisions.SetPushOutPairIterations(CollisionPushOutIts);
		}
	}

	void FSimulation::DebugDrawKinematicParticles(const int32 MinDebugLevel, const int32 MaxDebugLevel, const FColor& Color)
	{
#if CHAOS_DEBUG_DRAW
		using namespace Chaos;
		if (FDebugDrawQueue::IsDebugDrawingEnabled())
		{
			if ((ChaosImmediate_DebugDrawParticles >= MinDebugLevel) && (ChaosImmediate_DebugDrawParticles <= MaxDebugLevel))
			{
				DebugDraw::DrawParticleTransforms(SimulationSpaceTransform, Particles.GetActiveKinematicParticlesView());
			}
			if ((ChaosImmediate_DebugDrawShapes >= MinDebugLevel) && (ChaosImmediate_DebugDrawShapes <= MaxDebugLevel))
			{
				DebugDraw::DrawParticleShapes(SimulationSpaceTransform, Particles.GetActiveKinematicParticlesView(), Color);
			}
			if ((ChaosImmediate_DebugDrawBounds >= MinDebugLevel) && (ChaosImmediate_DebugDrawBounds <= MaxDebugLevel))
			{
				DebugDraw::DrawParticleBounds(SimulationSpaceTransform, Particles.GetActiveKinematicParticlesView(), Color);
			}
		}
#endif
	}


	void FSimulation::DebugDrawDynamicParticles(const int32 MinDebugLevel, const int32 MaxDebugLevel, const FColor& Color)
	{
#if CHAOS_DEBUG_DRAW
		using namespace Chaos;
		if (FDebugDrawQueue::IsDebugDrawingEnabled())
		{
			if ((ChaosImmediate_DebugDrawParticles >= MinDebugLevel) && (ChaosImmediate_DebugDrawParticles <= MaxDebugLevel))
			{
				DebugDraw::DrawParticleTransforms(SimulationSpaceTransform, Particles.GetActiveParticlesView());
			}
			if ((ChaosImmediate_DebugDrawShapes >= MinDebugLevel) && (ChaosImmediate_DebugDrawShapes <= MaxDebugLevel))
			{
				DebugDraw::DrawParticleShapes(SimulationSpaceTransform, Particles.GetActiveParticlesView(), Color);
			}
			if ((ChaosImmediate_DebugDrawBounds >= MinDebugLevel) && (ChaosImmediate_DebugDrawBounds <= MaxDebugLevel))
			{
				DebugDraw::DrawParticleBounds(SimulationSpaceTransform, Particles.GetActiveParticlesView(), Color);
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
				DebugDraw::DrawCollisions(SimulationSpaceTransform, Collisions, ColorScale);
			}
			if ((ChaosImmediate_DebugDrawJoints >= MinDebugLevel) && (ChaosImmediate_DebugDrawJoints <= MaxDebugLevel))
			{
				DebugDraw::DrawJointConstraints(SimulationSpaceTransform, Joints, ColorScale, (uint32)ChaosImmediate_DebugDrawJointFeatures);
			}
		}
#endif
	}
}
