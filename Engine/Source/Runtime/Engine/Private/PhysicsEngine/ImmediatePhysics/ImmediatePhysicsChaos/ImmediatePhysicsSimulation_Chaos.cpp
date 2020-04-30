// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsSimulation_Chaos.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsActorHandle_Chaos.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsJointHandle_Chaos.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsStats.h"

#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/ConstraintInstance.h"

#include "Chaos/ChaosDebugDraw.h"
#include "Chaos/Collision/NarrowPhase.h"
#include "Chaos/Collision/ParticlePairBroadPhase.h"
#include "Chaos/Collision/ParticlePairCollisionDetector.h"
#include "Chaos/DebugDrawQueue.h"
#include "Chaos/Evolution/PBDMinEvolution.h"
#include "Chaos/Joint/ChaosJointLog.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/PBDConstraintRule.h"
#include "Chaos/PBDJointConstraints.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "ChaosLog.h"


//PRAGMA_DISABLE_OPTIMIZATION

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
float ChaosImmediate_Evolution_BoundsExtension = 0.0f;
FAutoConsoleVariableRef CVarChaosImmPhysStepTime(TEXT("p.Chaos.ImmPhys.StepTime"), ChaosImmediate_Evolution_StepTime, TEXT("Override step time (if not zero)"));
FAutoConsoleVariableRef CVarChaosImmPhysNumSteps(TEXT("p.Chaos.ImmPhys.NumSteps"), ChaosImmediate_Evolution_NumSteps, TEXT("Override num steps (if not zero)"));
FAutoConsoleVariableRef CVarChaosImmPhysInitialStepTime(TEXT("p.Chaos.ImmPhys.InitialStepTime"), ChaosImmediate_Evolution_InitialStepTime, TEXT("Initial step time (then calculated from rolling average)"));
FAutoConsoleVariableRef CVarChaosImmPhysDeltaTimeCount(TEXT("p.Chaos.ImmPhys.DeltaTimeCount"), ChaosImmediate_Evolution_DeltaTimeCount, TEXT("The number of ticks over which the moving average is calculated"));
FAutoConsoleVariableRef CVarChaosImmPhysIterations(TEXT("p.Chaos.ImmPhys.Iterations"), ChaosImmediate_Evolution_Iterations, TEXT("Override number of constraint solver loops in immediate physics (if >= 0)"));
FAutoConsoleVariableRef CVarChaosImmPhysPushOutIterations(TEXT("p.Chaos.ImmPhys.PushOutIterations"), ChaosImmediate_Evolution_PushOutIterations, TEXT("Override number of solver push-out loops (if >= 0)"));
FAutoConsoleVariableRef CVarChaosImmPhysBoundsExtension(TEXT("p.Chaos.ImmPhys.BoundsExtension"), ChaosImmediate_Evolution_BoundsExtension, TEXT("Bounds are grown by this fraction of their size (should be >= 0.0)"));

float ChaosImmediate_Evolution_MinStepTime = 0.01f;
float ChaosImmediate_Evolution_FixedStepTime = -1.0f;
float ChaosImmediate_Evolution_FixedStepTolerance = 0.05f;
FAutoConsoleVariableRef CVarChaosImmPhysMinStepTime(TEXT("p.Chaos.ImmPhys.MinStepTime"), ChaosImmediate_Evolution_MinStepTime, TEXT("If non-zero, then if step time is lower than this, go into fixed step mode with this timestep."));
FAutoConsoleVariableRef CVarChaosImmPhysFixedStepTime(TEXT("p.Chaos.ImmPhys.FixedStepTime"), ChaosImmediate_Evolution_FixedStepTime, TEXT("Override fixed step time mode: fixed step time (if positive); variable time mode (if zero); asset defined (if negative)"));
FAutoConsoleVariableRef CVarChaosImmPhysFixedStepTolerance(TEXT("p.Chaos.ImmPhys.FixedStepTolerance"), ChaosImmediate_Evolution_FixedStepTolerance, TEXT("Tiem remainder required to add a new step (fraction of FixedStepTime)"));

int32 ChaosImmediate_Collision_Enabled = 1;
int32 ChaosImmediate_Collision_PairIterations = -1;
int32 ChaosImmediate_Collision_PushOutPairIterations = 0;	// Force disabled - not compatible with ECollisionApplyType::Position
int32 ChaosImmediate_Collision_Priority = 1;
float ChaosImmediate_Collision_CullDistance = 1.0f;
float ChaosImmediate_Collision_ShapePadding = 0;
int32 ChaosImmediate_Collision_DeferNarrowPhase = 1;
int32 ChaosImmediate_Collision_UseManifolds = 0;
FAutoConsoleVariableRef CVarChaosImmPhysCollisionDisable(TEXT("p.Chaos.ImmPhys.Collision.Enabled"), ChaosImmediate_Collision_Enabled, TEXT("Enable/Disable collisions in Immediate Physics."));
FAutoConsoleVariableRef CVarChaosImmPhysCollisionPairIterations(TEXT("p.Chaos.ImmPhys.Collision.PairIterations"), ChaosImmediate_Collision_PairIterations, TEXT("Override collision pair iterations (if >= 0)"));
FAutoConsoleVariableRef CVarChaosImmPhysCollisionPushOutPairIterations(TEXT("p.Chaos.ImmPhys.Collision.PushOutPairIterations"), ChaosImmediate_Collision_PushOutPairIterations, TEXT("Override collision push-out pair iterations (if >= 0)"));
FAutoConsoleVariableRef CVarChaosImmPhysCollisionPriority(TEXT("p.Chaos.ImmPhys.Collision.Priority"), ChaosImmediate_Collision_Priority, TEXT("Set the Collision constraint sort order (Joints have priority 0)"));
FAutoConsoleVariableRef CVarChaosImmPhysCollisionCullDistance(TEXT("p.Chaos.ImmPhys.Collision.CullDistance"), ChaosImmediate_Collision_CullDistance, TEXT("CullDistance"));
FAutoConsoleVariableRef CVarChaosImmPhysCollisionShapePadding(TEXT("p.Chaos.ImmPhys.Collision.ShapePadding"), ChaosImmediate_Collision_ShapePadding, TEXT("ShapePadding"));
FAutoConsoleVariableRef CVarChaosImmPhysCollisionDeferNarrowPhase(TEXT("p.Chaos.ImmPhys.Collision.DeferNarrowPhase"), ChaosImmediate_Collision_DeferNarrowPhase, TEXT("Create contacts for all broadphase pairs, perform NarrowPhase later."));
FAutoConsoleVariableRef CVarChaosImmPhysCollisionUseManifolds(TEXT("p.Chaos.ImmPhys.Collision.UseManifolds"), ChaosImmediate_Collision_UseManifolds, TEXT("Enable/Disable use of manifoldes in collision."));

int32 ChaosImmediate_Joint_PairIterations = -1;
int32 ChaosImmediate_Joint_PushOutPairIterations = -1;
float ChaosImmediate_Joint_SwingTwistAngleTolerance = 1.0e-6f;
float ChaosImmediate_Joint_PositionTolerance = 0.05f;
float ChaosImmediate_Joint_AngleTolerance = 0.001f;
int32 ChaosImmediate_Joint_EnableTwistLimits = 1;
int32 ChaosImmediate_Joint_EnableSwingLimits = 1;
int32 ChaosImmediate_Joint_EnableDrives = 1;
float ChaosImmediate_Joint_LinearProjection = -1.0f;
float ChaosImmediate_Joint_AngularProjection = -1.0f;
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
float ChaosImmediate_Joint_AngularPositionCorrection = 0.0f;
float ChaosImmediate_Joint_ProjectionInvMassScale = 0.0f;
float ChaosImmediate_Joint_VelProjectionInvMassScale = 1.0f;
FAutoConsoleVariableRef CVarChaosImmPhysJointPairIterations(TEXT("p.Chaos.ImmPhys.Joint.PairIterations"), ChaosImmediate_Joint_PairIterations, TEXT("Override joint pair iterations (if >= 0)"));
FAutoConsoleVariableRef CVarChaosImmPhysJointPushOutPairIterations(TEXT("p.Chaos.ImmPhys.Joint.PushOutPairIterations"), ChaosImmediate_Joint_PushOutPairIterations, TEXT("Override joint push-out pair iterations (if >= 0)"));
FAutoConsoleVariableRef CVarChaosImmPhysJointSwingTwistAngleTolerance(TEXT("p.Chaos.ImmPhys.Joint.SwingTwistAngleTolerance"), ChaosImmediate_Joint_SwingTwistAngleTolerance, TEXT("SwingTwistAngleTolerance."));
FAutoConsoleVariableRef CVarChaosImmPhysJointPositionTolerance(TEXT("p.Chaos.ImmPhys.Joint.PositionTolerance"), ChaosImmediate_Joint_PositionTolerance, TEXT("PositionTolerance."));
FAutoConsoleVariableRef CVarChaosImmPhysJointAngleTolerance(TEXT("p.Chaos.ImmPhys.Joint.AngleTolerance"), ChaosImmediate_Joint_AngleTolerance, TEXT("PositionTolerance."));
FAutoConsoleVariableRef CVarChaosImmPhysJointEnableTwistLimits(TEXT("p.Chaos.ImmPhys.Joint.EnableTwistLimits"), ChaosImmediate_Joint_EnableTwistLimits, TEXT("EnableTwistLimits."));
FAutoConsoleVariableRef CVarChaosImmPhysJointEnableSwingLimits(TEXT("p.Chaos.ImmPhys.Joint.EnableSwingLimits"), ChaosImmediate_Joint_EnableSwingLimits, TEXT("EnableSwingLimits."));
FAutoConsoleVariableRef CVarChaosImmPhysJointEnableDrives(TEXT("p.Chaos.ImmPhys.Joint.EnableDrives"), ChaosImmediate_Joint_EnableDrives, TEXT("EnableDrives."));
FAutoConsoleVariableRef CVarChaosImmPhysJointLinearProjection(TEXT("p.Chaos.ImmPhys.Joint.LinearProjection"), ChaosImmediate_Joint_LinearProjection, TEXT("6Dof joint projection amount override (if >= 0)."));
FAutoConsoleVariableRef CVarChaosImmPhysJointAngularProjection(TEXT("p.Chaos.ImmPhys.Joint.AngularProjection"), ChaosImmediate_Joint_AngularProjection, TEXT("6Dof joint projection amount override (if >= 0)."));
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
FAutoConsoleVariableRef CVarChaosImmPhysJointProjectionInvMassScale(TEXT("p.Chaos.ImmPhys.Joint.ProjectionInvMassScale"), ChaosImmediate_Joint_ProjectionInvMassScale, TEXT("Scale applied to parent body's inverse mass during projection [0-1]"));
FAutoConsoleVariableRef CVarChaosImmPhysJointVelProjectionInvMassScale(TEXT("p.Chaos.ImmPhys.Joint.VelProjectionInvMassScale"), ChaosImmediate_Joint_VelProjectionInvMassScale, TEXT("Scale applied to parent body's inverse mass during projection for velocity fixup [0-1]"));

//
// Even more temp that the above...
//
int32 ChaosImmediate_UsePositionSolver = 1;
FAutoConsoleVariableRef CVarChaosImmPhysUsePositionSolver(TEXT("p.Chaos.ImmPhys.UsePositionSolver"), ChaosImmediate_UsePositionSolver, TEXT("Use position based collision solver for Immediate Physics (default true)"));

//
// end remove when finished
//
//////////////////////////////////////////////////////////////////////////


// DebugDraw CVars
#if UE_BUILD_DEBUG
int32 ChaosImmediate_DebugDrawParticles = 0;
int32 ChaosImmediate_DebugDrawShapes = 1;
int32 ChaosImmediate_DebugDrawShowKinematics = 1;
int32 ChaosImmediate_DebugDrawShowDynamics = 1;
int32 ChaosImmediate_DebugDrawBounds = 0;
int32 ChaosImmediate_DebugDrawCollisions = 0;
int32 ChaosImmediate_DebugDrawJoints = 1;
#else
int32 ChaosImmediate_DebugDrawParticles = 0;
int32 ChaosImmediate_DebugDrawShapes = 0;
int32 ChaosImmediate_DebugDrawShowKinematics = 1;
int32 ChaosImmediate_DebugDrawShowDynamics = 1;
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
FAutoConsoleVariableRef CVarChaosImmPhysDebugDrawShowKinematics(TEXT("p.Chaos.ImmPhys.DebugDrawShowKinematics"), ChaosImmediate_DebugDrawShowKinematics, TEXT("Show kinematics if shape debug draw is enabled"));
FAutoConsoleVariableRef CVarChaosImmPhysDebugDrawShowDynamics(TEXT("p.Chaos.ImmPhys.DebugDrawShowDynamics"), ChaosImmediate_DebugDrawShowDynamics, TEXT("Show dynamics if shape debug draw is enabled"));
FAutoConsoleVariableRef CVarChaosImmPhysDebugDrawBounds(TEXT("p.Chaos.ImmPhys.DebugDrawBounds"), ChaosImmediate_DebugDrawBounds, TEXT("Draw Particle Bounds (0 = never; 1 = end of frame; 2 = begin and end of frame; 3 = post-integate, post-apply and post-applypushout;)."));
FAutoConsoleVariableRef CVarChaosImmPhysDebugDrawCollisions(TEXT("p.Chaos.ImmPhys.DebugDrawCollisions"), ChaosImmediate_DebugDrawCollisions, TEXT("Draw Collisions (0 = never; 1 = end of frame; 2 = begin and end of frame; 3 = post-integate, post-apply and post-applypushout;)"));
FAutoConsoleVariableRef CVarChaosImmPhysDebugDrawJoints(TEXT("p.Chaos.ImmPhys.DebugDrawJoints"), ChaosImmediate_DebugDrawJoints, TEXT("Draw Joints. (0 = never; 1 = end of frame; 2 = begin and end of frame; 3 = post-integate, post-apply and post-applypushout; 4 = each Apply step)."));
FAutoConsoleVariableRef CVarChaosImmPhysDebugDrawJointFeatures(TEXT("p.Chaos.ImmPhys.DebugDrawJointFeatures"), ChaosImmediate_DebugDrawJointFeatures, TEXT("Joint features mask (see EDebugDrawJointFeature)."));

namespace ImmediatePhysics_Chaos
{
	struct FSimulation::FImplementation
	{
	public:
		using FCollisionConstraints = Chaos::FPBDCollisionConstraints;
		using FCollisionDetector = Chaos::FParticlePairCollisionDetector;
		using FParticleHandle = Chaos::TGeometryParticleHandle<FReal, Dimensions>;
		using FParticlePair = Chaos::TVector<Chaos::TGeometryParticleHandle<Chaos::FReal, 3>*, 2>;
		using FRigidParticleSOAs = Chaos::TPBDRigidsSOAs<FReal, 3>;

		FImplementation()
			: Particles()
			, Joints()
			, Collisions(Particles, CollidedParticles, ParticleMaterials, PerParticleMaterials, 0, 0, ChaosImmediate_Collision_CullDistance, ChaosImmediate_Collision_ShapePadding)
			, BroadPhase(ActivePotentiallyCollidingPairs, ChaosImmediate_Collision_CullDistance)
			, NarrowPhase()
			, CollisionDetector(BroadPhase, NarrowPhase, Collisions)
			, JointsRule(0, Joints)
			, CollisionsRule(1, Collisions)
			, Evolution(Particles, ParticlePrevXs, ParticlePrevRs, CollisionDetector, ChaosImmediate_Evolution_BoundsExtension)
			, NumActiveDynamicActorHandles(0)
			, SimulationSpaceTransform(FTransform::Identity)
			, RollingAverageStepTime(ChaosImmediate_Evolution_InitialStepTime)
			, NumRollingAverageStepTimes(1)
			, MaxNumRollingAverageStepTimes(ChaosImmediate_Evolution_DeltaTimeCount)
			, bActorsDirty(false)
		{
			Particles.GetParticleHandles().AddArray(&ParticleMaterials);
			Particles.GetParticleHandles().AddArray(&PerParticleMaterials);
			Particles.GetParticleHandles().AddArray(&ParticlePrevXs);
			Particles.GetParticleHandles().AddArray(&ParticlePrevRs);

			Evolution.AddConstraintRule(&CollisionsRule);
			Evolution.AddConstraintRule(&JointsRule);
		}

		~FImplementation()
		{
		}

		// @todo(ccaulfield): Look into these...
		TArray<FParticlePair> ActivePotentiallyCollidingPairs;
		Chaos::TArrayCollectionArray<bool> CollidedParticles;
		Chaos::TArrayCollectionArray<Chaos::TSerializablePtr<Chaos::FChaosPhysicsMaterial>> ParticleMaterials;
		Chaos::TArrayCollectionArray<TUniquePtr<Chaos::FChaosPhysicsMaterial>> PerParticleMaterials;
		Chaos::TArrayCollectionArray<Chaos::FVec3> ParticlePrevXs;
		Chaos::TArrayCollectionArray<Chaos::FRotation3> ParticlePrevRs;

		FRigidParticleSOAs Particles;
		Chaos::FPBDJointConstraints Joints;
		FCollisionConstraints Collisions;
		Chaos::FParticlePairBroadPhase BroadPhase;
		Chaos::FNarrowPhase NarrowPhase;
		FCollisionDetector CollisionDetector;
		Chaos::TSimpleConstraintRule<Chaos::FPBDJointConstraints> JointsRule;
		Chaos::TSimpleConstraintRule<FCollisionConstraints> CollisionsRule;
		Chaos::FPBDMinEvolution Evolution;

		/** Mapping from entity index to handle */
		// @todo(ccaulfield): we now have handles pointing to handles which is inefficient - we can do better than this, but don't want to change API yet
		TArray<FActorHandle*> ActorHandles;
		int32 NumActiveDynamicActorHandles;

		/** Mapping from constraint index to handle */
		TArray<FJointHandle*> JointHandles;

		/** Slow to access. */
		// @todo(ccaulfield): Optimize
		TMap<const FParticleHandle*, TSet<const FParticleHandle*>> IgnoreCollisionParticlePairTable;

		TArray<FParticlePair> PotentiallyCollidingPairs;

		FTransform SimulationSpaceTransform;

		FReal FixedStepTime;
		FReal RollingAverageStepTime;
		int32 NumRollingAverageStepTimes;
		int32 MaxNumRollingAverageStepTimes;

		bool bActorsDirty;
	};

	//
	//
	//

	FSimulation::FSimulation()
	{
		using namespace Chaos;

		Implementation = MakeUnique<FImplementation>();

		// RBAN collision customization
		Implementation->Collisions.DisableHandles();
		Implementation->Collisions.SetApplyType(ECollisionApplyType::Position);
		Implementation->NarrowPhase.GetContext().bFilteringEnabled = false;
		Implementation->NarrowPhase.GetContext().bDeferUpdate = true;
		Implementation->NarrowPhase.GetContext().bAllowManifolds = false;


#if CHAOS_DEBUG_DRAW
		Implementation->Evolution.SetPostIntegrateCallback(
			[this]()
			{
				DebugDrawDynamicParticles(3, 4, FColor(32, 32, 0));
				DebugDrawConstraints(3, 4, 0.3f);
			});
		Implementation->Evolution.SetPostApplyCallback(
			[this]()
			{
				DebugDrawDynamicParticles(3, 3, FColor(128, 128, 0));
				DebugDrawConstraints(3, 3, 0.6f);
			});
		Implementation->Evolution.SetPostApplyPushOutCallback(
			[this]()
			{
				DebugDrawDynamicParticles(3, 3, FColor(255, 255, 0));
				DebugDrawConstraints(3, 3, 1.0f);
			});
		Implementation->Collisions.SetPostApplyCallback(
			[this](const float Dt, const TArray<FPBDCollisionConstraintHandle*>& InConstraintHandles)
			{
				if (ChaosImmediate_DebugDrawCollisions == 4)
				{
					DebugDraw::DrawCollisions(Implementation->SimulationSpaceTransform, Implementation->Collisions, 0.3f);
				}
				DebugDrawDynamicParticles(4, 4, FColor(128, 128, 0));
			});
		Implementation->Collisions.SetPostApplyPushOutCallback(
			[this](const float Dt, const TArray<FPBDCollisionConstraintHandle*>& InConstraintHandles, bool bRequiresAnotherIteration)
			{
				if (ChaosImmediate_DebugDrawCollisions == 4)
				{
					DebugDraw::DrawCollisions(Implementation->SimulationSpaceTransform, Implementation->Collisions, 0.6f);
				}
			});
		Implementation->Joints.SetPreApplyCallback(
			[this](const float Dt, const TArray<FPBDJointConstraintHandle*>& InConstraintHandles)
			{
				if (ChaosImmediate_DebugDrawJoints == 4)
				{
					DebugDraw::DrawJointConstraints(Implementation->SimulationSpaceTransform, InConstraintHandles, 0.3f);
				}
			});
		Implementation->Joints.SetPostApplyCallback(
			[this](const float Dt, const TArray<FPBDJointConstraintHandle*>& InConstraintHandles)
			{
				if (ChaosImmediate_DebugDrawJoints == 4)
				{
					DebugDraw::DrawJointConstraints(Implementation->SimulationSpaceTransform, InConstraintHandles, 0.6f);
				}
				DebugDrawDynamicParticles(4, 4, FColor(0, 128, 0));
			});
		Implementation->Joints.SetPostProjectCallback(
			[this](const float Dt, const TArray<FPBDJointConstraintHandle*>& InConstraintHandles)
			{
				if (ChaosImmediate_DebugDrawJoints == 4)
				{
					DebugDraw::DrawJointConstraints(Implementation->SimulationSpaceTransform, InConstraintHandles, 0.6f);
				}
			});
#endif
	}

	FSimulation::~FSimulation()
	{
		using namespace Chaos;

		for (FActorHandle* ActorHandle : Implementation->ActorHandles)
		{
			delete ActorHandle;
		}
		Implementation->ActorHandles.Empty();

		for (FJointHandle* JointHandle : Implementation->JointHandles)
		{
			delete JointHandle;
		}
		Implementation->JointHandles.Empty();
	}

	int32 FSimulation::NumActors() const
	{
		return Implementation->ActorHandles.Num();
	}

	FActorHandle* FSimulation::GetActorHandle(int32 ActorHandleIndex)
	{
		return Implementation->ActorHandles[ActorHandleIndex];
	}
	
	const FActorHandle* FSimulation::GetActorHandle(int32 ActorHandleIndex) const
	{
		return Implementation->ActorHandles[ActorHandleIndex];
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
		// @todo(ccaulfield): Shared materials
		// @todo(ccaulfield): Add colliding particle pairs

		using namespace Chaos;

		FActorHandle* ActorHandle = new FActorHandle(Implementation->Particles, Implementation->ParticlePrevXs, Implementation->ParticlePrevRs, ActorType, BodyInstance, Transform);
		int ActorIndex = Implementation->ActorHandles.Add(ActorHandle);

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

		Implementation->ParticleMaterials.Add(MakeSerializable(Material));
		Implementation->PerParticleMaterials.Add(MoveTemp(Material));
		Implementation->CollidedParticles.Add(false);
		Implementation->ParticlePrevXs.Add(ActorHandle->GetParticle()->X());
		Implementation->ParticlePrevRs.Add(ActorHandle->GetParticle()->R());

		Implementation->bActorsDirty = true;

		return ActorHandle;
	}

	void FSimulation::DestroyActor(FActorHandle* ActorHandle)
	{
		// @todo(ccaulfield): FActorHandle could remember its index to optimize this
		// @todo(ccaulfield): Remove colliding particle pairs

		int32 Index = Implementation->ActorHandles.Remove(ActorHandle);
		delete ActorHandle;

		Implementation->ParticleMaterials.RemoveAt(Index, 1);
		Implementation->PerParticleMaterials.RemoveAt(Index, 1);
		Implementation->CollidedParticles.RemoveAt(Index, 1);
		Implementation->ParticlePrevXs.RemoveAt(Index, 1);
		Implementation->ParticlePrevRs.RemoveAt(Index, 1);

		Implementation->bActorsDirty = true;
	}

	FJointHandle* FSimulation::CreateJoint(FConstraintInstance* ConstraintInstance, FActorHandle* Body1, FActorHandle* Body2)
	{
		FJointHandle* JointHandle = new FJointHandle(&Implementation->Joints, ConstraintInstance, Body1, Body2);
		Implementation->JointHandles.Add(JointHandle);
		return JointHandle;
	}

	void FSimulation::DestroyJoint(FJointHandle* JointHandle)
	{
		// @todo(ccaulfield): FJointHandle could remember its index to optimize this
		Implementation->JointHandles.Remove(JointHandle);
		delete JointHandle;
	}

	void FSimulation::SetNumActiveBodies(int32 InNumActiveActorHandles)
	{
		if (InNumActiveActorHandles == Implementation->NumActiveDynamicActorHandles)
		{
			return;
		}

		// @todo(ccaulfield): can be optimized, but I think we end up with kinematic at the start and statics at the end of the 
		// list - maybe split them up or put kinematics at the end (in a way that does not impact particle order).
		Implementation->NumActiveDynamicActorHandles = 0;
		for (int32 ActorHandleIndex = 0; ActorHandleIndex < Implementation->ActorHandles.Num(); ++ActorHandleIndex)
		{
			FActorHandle* Handle = GetActorHandle(ActorHandleIndex);
			if (Handle->GetIsKinematic())
			{
				continue;
			}

			if (Implementation->NumActiveDynamicActorHandles < InNumActiveActorHandles)
			{
				Handle->SetEnabled(true);
				++Implementation->NumActiveDynamicActorHandles;
			}
			else
			{
				Handle->SetEnabled(false);
			}
		}

		Implementation->bActorsDirty = true;
	}

	void FSimulation::SetIgnoreCollisionPairTable(const TArray<FIgnorePair>& InIgnoreCollisionPairTable)
	{
		using namespace Chaos;

		Implementation->IgnoreCollisionParticlePairTable.Reset();
		for (const FIgnorePair& IgnorePair : InIgnoreCollisionPairTable)
		{
			if(!IgnorePair.A || !IgnorePair.B)
			{
				continue;
			}

			const TGeometryParticleHandle<FReal, Dimensions>* ParticleA = IgnorePair.A->GetParticle();
			const TGeometryParticleHandle<FReal, Dimensions>* ParticleB = IgnorePair.B->GetParticle();

			Implementation->IgnoreCollisionParticlePairTable.FindOrAdd(ParticleA).Add(ParticleB); 
			Implementation->IgnoreCollisionParticlePairTable.FindOrAdd(ParticleB).Add(ParticleA);
		}

		Implementation->PotentiallyCollidingPairs.Empty();
		int NumActorHandles = Implementation->ActorHandles.Num();
		for (int ActorHandleIndex0 = 0; ActorHandleIndex0 < NumActorHandles; ++ActorHandleIndex0)
		{
			FActorHandle* ActorHandle0 = Implementation->ActorHandles[ActorHandleIndex0];
			TGeometryParticleHandle<FReal, Dimensions>* Particle0 = ActorHandle0->GetParticle();
			//TPBDRigidParticleHandle<FReal, Dimensions>* Rigid0 = Particle0->CastToRigidParticle();
			//if (Rigid0 && (Rigid0->ObjectState() == EObjectStateType::Dynamic))
			{
				for (int ActorHandleIndex1 = ActorHandleIndex0 + 1; ActorHandleIndex1 < NumActorHandles; ++ActorHandleIndex1)
				{
					FActorHandle* ActorHandle1 = Implementation->ActorHandles[ActorHandleIndex1];
					TGeometryParticleHandle<FReal, Dimensions>* Particle1 = ActorHandle1->GetParticle();

					const TSet<const typename FImplementation::FParticleHandle*>* Particle0IgnoreSet = Implementation->IgnoreCollisionParticlePairTable.Find(Particle0);
					bool bIgnoreActorHandlePair = (Particle0IgnoreSet != nullptr) && Particle0IgnoreSet->Contains(Particle1);
					if (!bIgnoreActorHandlePair)
					{
						Implementation->PotentiallyCollidingPairs.Emplace(typename FImplementation::FParticlePair(Particle0, Particle1));
					}
				}
			}
		}

		Implementation->bActorsDirty = true;
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

		Implementation->bActorsDirty = true;
	}

	void FSimulation::UpdateActivePotentiallyCollidingPairs()
	{
		using namespace Chaos;

		Implementation->ActivePotentiallyCollidingPairs.Reset();
		for (const typename FImplementation::FParticlePair& ParticlePair : Implementation->PotentiallyCollidingPairs)
		{
			bool bAnyDisabled = TGenericParticleHandle<FReal, 3>(ParticlePair[0])->Disabled() || TGenericParticleHandle<FReal, 3>(ParticlePair[1])->Disabled();
			bool bAnyDynamic = TGenericParticleHandle<FReal, 3>(ParticlePair[0])->IsDynamic() || TGenericParticleHandle<FReal, 3>(ParticlePair[1])->IsDynamic();
			if (bAnyDynamic && !bAnyDisabled)
			{
				Implementation->ActivePotentiallyCollidingPairs.Add(ParticlePair);
			}
		}
	}

	void FSimulation::SetSimulationSpaceTransform(const FTransform& Transform)
	{ 
		Implementation->SimulationSpaceTransform = Transform;
		Implementation->NarrowPhase.GetContext().SpaceTransform = Transform;	// @todo(chaos): remove when manifolds are fixed or removed
	}


	FReal FSimulation::UpdateStepTime(const FReal DeltaTime, const FReal MaxStepTime)
	{
		Implementation->RollingAverageStepTime = Implementation->RollingAverageStepTime + (DeltaTime - Implementation->RollingAverageStepTime) / Implementation->NumRollingAverageStepTimes;
		Implementation->RollingAverageStepTime = FMath::Min(Implementation->RollingAverageStepTime, MaxStepTime);
		Implementation->NumRollingAverageStepTimes = FMath::Min(Implementation->NumRollingAverageStepTimes + 1, Implementation->MaxNumRollingAverageStepTimes);
		return Implementation->RollingAverageStepTime;
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
			FPBDJointSolverSettings JointsSettings = Implementation->Joints.GetSettings();
			JointsSettings.SwingTwistAngleTolerance = ChaosImmediate_Joint_SwingTwistAngleTolerance;
			JointsSettings.PositionTolerance = ChaosImmediate_Joint_PositionTolerance;
			JointsSettings.AngleTolerance = ChaosImmediate_Joint_AngleTolerance;
			JointsSettings.MinParentMassRatio = ChaosImmediate_Joint_MinParentMassRatio;
			JointsSettings.MaxInertiaRatio = ChaosImmediate_Joint_MaxInertiaRatio;
			JointsSettings.AngularConstraintPositionCorrection = ChaosImmediate_Joint_AngularPositionCorrection;
			JointsSettings.ProjectionInvMassScale = ChaosImmediate_Joint_ProjectionInvMassScale;
			JointsSettings.VelProjectionInvMassScale = ChaosImmediate_Joint_VelProjectionInvMassScale;
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
			Implementation->Joints.SetSettings(JointsSettings);

			Implementation->Collisions.SetShapePadding(ChaosImmediate_Collision_ShapePadding);
			Implementation->Collisions.SetCollisionsEnabled(ChaosImmediate_Collision_Enabled != 0);
			Implementation->CollisionsRule.SetPriority(ChaosImmediate_Collision_Priority);

			Implementation->Collisions.SetCullDistance(ChaosImmediate_Collision_CullDistance);
			Implementation->BroadPhase.SetCullDustance(ChaosImmediate_Collision_CullDistance);
			Implementation->Evolution.SetBoundsExtension(ChaosImmediate_Evolution_BoundsExtension);

			Implementation->NarrowPhase.GetContext().bDeferUpdate = (ChaosImmediate_Collision_DeferNarrowPhase != 0);
			Implementation->NarrowPhase.GetContext().bAllowManifolds = (ChaosImmediate_Collision_UseManifolds != 0);

			if (ChaosImmediate_UsePositionSolver)
			{
				Implementation->Collisions.SetApplyType(ECollisionApplyType::Position);
			}
			else
			{
				Implementation->Collisions.SetApplyType(ECollisionApplyType::Velocity);
			}

			if (ChaosImmediate_Evolution_StepTime > 0)
			{
				StepTime = ChaosImmediate_Evolution_StepTime;
			}
			if (ChaosImmediate_Evolution_NumSteps > 0)
			{
				NumSteps = ChaosImmediate_Evolution_NumSteps;
			}

			SetSolverIterations(
				ChaosImmediate_Evolution_FixedStepTime,
				ChaosImmediate_Evolution_Iterations,
				ChaosImmediate_Joint_PairIterations,
				ChaosImmediate_Collision_PairIterations,
				ChaosImmediate_Evolution_PushOutIterations,
				ChaosImmediate_Joint_PushOutPairIterations,
				ChaosImmediate_Collision_PushOutPairIterations);
		}

		DebugDrawKinematicParticles(2, 2, FColor(128, 0, 0));
		DebugDrawDynamicParticles(2, 2, FColor(192, 192, 0));
		DebugDrawConstraints(2, 2, 0.7f);

		// Fixed timestep mode DT (Only used if > 0.0f)
		FReal FixedStepTime = Implementation->FixedStepTime;

		// Use fixed step mode anyway if StepTime is too low
		// This can prevent collision push resolution introducing large velocities at small DTs
		if ((FixedStepTime <= 0.0f) && (StepTime < ChaosImmediate_Evolution_MinStepTime))
		{
			FixedStepTime = ChaosImmediate_Evolution_MinStepTime;
		}

		// If using FixedStep mode, calculate the number of steps and how far to rewind (if at all)
		FReal RewindTime = 0.0f;
		if (FixedStepTime > 0)
		{
			StepTime = FixedStepTime;
			NumSteps = FMath::FloorToInt(DeltaTime / StepTime);
			FReal RemainderTime = DeltaTime - NumSteps * StepTime;
			if (RemainderTime > ChaosImmediate_Evolution_FixedStepTolerance* StepTime)
			{
				++NumSteps;
				RewindTime = StepTime - RemainderTime;
			}
			NumSteps = FMath::Max(1, NumSteps);
		}

		// Handle new or deleted particles
		if (Implementation->bActorsDirty)
		{
			UpdateActivePotentiallyCollidingPairs();
			Implementation->bActorsDirty = false;
		}

		UE_LOG(LogChaosJoint, Verbose, TEXT("Simulate Dt = %f Steps %d x %f (Rewind %f)"), DeltaTime, NumSteps, StepTime, RewindTime);
		Implementation->Evolution.SetGravity(InGravity);
		Implementation->Evolution.Advance(StepTime, NumSteps, RewindTime);

		DebugDrawKinematicParticles(1, 4, FColor(128, 0, 0));
		DebugDrawDynamicParticles(1, 3, FColor(255, 255, 0));
		DebugDrawConstraints(1, 2, 1.0f);
	}

	void FSimulation::SetSolverIterations(const FReal InFixedDt, const int32 SolverIts, const int32 JointIts, const int32 CollisionIts, const int32 SolverPushOutIts, const int32 JointPushOutIts, const int32 CollisionPushOutIts)
	{
		if (InFixedDt >= 0.0f)
		{
			Implementation->FixedStepTime = InFixedDt;
		}

		if (SolverIts >= 0)
		{
			Implementation->Evolution.SetNumIterations(SolverIts);
		}
		if (SolverPushOutIts >= 0)
		{
			Implementation->Evolution.SetNumPushOutIterations(SolverPushOutIts);
		}

		if (JointIts >= 0)
		{
			Implementation->Joints.SetNumPairIterations(JointIts);
		}
		if (JointPushOutIts >= 0)
		{
			Implementation->Joints.SetNumPushOutPairIterations(JointPushOutIts);
		}

		if (CollisionIts >= 0)
		{
			Implementation->Collisions.SetPairIterations(CollisionIts);
		}
		if (CollisionPushOutIts >= 0)
		{
			Implementation->Collisions.SetPushOutPairIterations(CollisionPushOutIts);
		}
	}

	void FSimulation::DebugDrawKinematicParticles(const int32 MinDebugLevel, const int32 MaxDebugLevel, const FColor& Color)
	{
#if CHAOS_DEBUG_DRAW
		using namespace Chaos;
		if (FDebugDrawQueue::IsDebugDrawingEnabled())
		{
			if (!ChaosImmediate_DebugDrawShowKinematics)
			{
				return;
			}
			if ((ChaosImmediate_DebugDrawParticles >= MinDebugLevel) && (ChaosImmediate_DebugDrawParticles <= MaxDebugLevel))
			{
				DebugDraw::DrawParticleTransforms(Implementation->SimulationSpaceTransform, Implementation->Particles.GetActiveKinematicParticlesView());
			}
			if ((ChaosImmediate_DebugDrawShapes >= MinDebugLevel) && (ChaosImmediate_DebugDrawShapes <= MaxDebugLevel))
			{
				DebugDraw::DrawParticleShapes(Implementation->SimulationSpaceTransform, Implementation->Particles.GetActiveKinematicParticlesView(), Color);
			}
			if ((ChaosImmediate_DebugDrawBounds >= MinDebugLevel) && (ChaosImmediate_DebugDrawBounds <= MaxDebugLevel))
			{
				DebugDraw::DrawParticleBounds(Implementation->SimulationSpaceTransform, Implementation->Particles.GetActiveKinematicParticlesView(), Color);
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
			if (!ChaosImmediate_DebugDrawShowDynamics)
			{
				return;
			}
			if ((ChaosImmediate_DebugDrawParticles >= MinDebugLevel) && (ChaosImmediate_DebugDrawParticles <= MaxDebugLevel))
			{
				DebugDraw::DrawParticleTransforms(Implementation->SimulationSpaceTransform, Implementation->Particles.GetActiveParticlesView());
			}
			if ((ChaosImmediate_DebugDrawShapes >= MinDebugLevel) && (ChaosImmediate_DebugDrawShapes <= MaxDebugLevel))
			{
				DebugDraw::DrawParticleShapes(Implementation->SimulationSpaceTransform, Implementation->Particles.GetActiveParticlesView(), Color);
			}
			if ((ChaosImmediate_DebugDrawBounds >= MinDebugLevel) && (ChaosImmediate_DebugDrawBounds <= MaxDebugLevel))
			{
				DebugDraw::DrawParticleBounds(Implementation->SimulationSpaceTransform, Implementation->Particles.GetActiveParticlesView(), Color);
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
				DebugDraw::DrawCollisions(Implementation->SimulationSpaceTransform, Implementation->Collisions, ColorScale);
			}
			if ((ChaosImmediate_DebugDrawJoints >= MinDebugLevel) && (ChaosImmediate_DebugDrawJoints <= MaxDebugLevel))
			{
				DebugDraw::DrawJointConstraints(Implementation->SimulationSpaceTransform, Implementation->Joints, ColorScale, (uint32)ChaosImmediate_DebugDrawJointFeatures);
			}
		}
#endif
	}
}
