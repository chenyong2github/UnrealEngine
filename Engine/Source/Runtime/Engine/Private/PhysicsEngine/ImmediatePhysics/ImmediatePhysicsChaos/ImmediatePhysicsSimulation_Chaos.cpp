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
Chaos::FRealSingle ChaosImmediate_Evolution_StepTime = 0.0f;
int32 ChaosImmediate_Evolution_NumSteps = 0;
Chaos::FRealSingle ChaosImmediate_Evolution_InitialStepTime = 0.033f;
int32 ChaosImmediate_Evolution_DeltaTimeCount = 100;
int32 ChaosImmediate_Evolution_Iterations = -1;
int32 ChaosImmediate_Evolution_PushOutIterations = -1;
Chaos::FRealSingle ChaosImmediate_Evolution_BoundsExtension = 0.0f;
FAutoConsoleVariableRef CVarChaosImmPhysStepTime(TEXT("p.Chaos.ImmPhys.StepTime"), ChaosImmediate_Evolution_StepTime, TEXT("Override step time (if not zero)"));
FAutoConsoleVariableRef CVarChaosImmPhysNumSteps(TEXT("p.Chaos.ImmPhys.NumSteps"), ChaosImmediate_Evolution_NumSteps, TEXT("Override num steps (if not zero)"));
FAutoConsoleVariableRef CVarChaosImmPhysInitialStepTime(TEXT("p.Chaos.ImmPhys.InitialStepTime"), ChaosImmediate_Evolution_InitialStepTime, TEXT("Initial step time (then calculated from rolling average)"));
FAutoConsoleVariableRef CVarChaosImmPhysDeltaTimeCount(TEXT("p.Chaos.ImmPhys.DeltaTimeCount"), ChaosImmediate_Evolution_DeltaTimeCount, TEXT("The number of ticks over which the moving average is calculated"));
FAutoConsoleVariableRef CVarChaosImmPhysIterations(TEXT("p.Chaos.ImmPhys.Iterations"), ChaosImmediate_Evolution_Iterations, TEXT("Override number of constraint solver loops in immediate physics (if >= 0)"));
FAutoConsoleVariableRef CVarChaosImmPhysPushOutIterations(TEXT("p.Chaos.ImmPhys.PushOutIterations"), ChaosImmediate_Evolution_PushOutIterations, TEXT("Override number of solver push-out loops (if >= 0)"));
FAutoConsoleVariableRef CVarChaosImmPhysBoundsExtension(TEXT("p.Chaos.ImmPhys.BoundsExtension"), ChaosImmediate_Evolution_BoundsExtension, TEXT("Bounds are grown by this fraction of their size (should be >= 0.0)"));

Chaos::FRealSingle ChaosImmediate_Evolution_SimSpaceCentrifugalAlpha = 1.0f;
Chaos::FRealSingle ChaosImmediate_Evolution_SimSpaceCoriolisAlpha = 0.5f;
Chaos::FRealSingle ChaosImmediate_Evolution_SimSpaceEulerAlpha = 1.0f;
FAutoConsoleVariableRef CVarChaosImmPhysSimSpaceCentrifugalAlpha(TEXT("p.Chaos.ImmPhys.SimSpaceCentrifugalAlpha"), ChaosImmediate_Evolution_SimSpaceCentrifugalAlpha, TEXT("Settings for simulation space system for rigid body nodes"));
FAutoConsoleVariableRef CVarChaosImmPhysSimSpaceCoriolisAlpha(TEXT("p.Chaos.ImmPhys.SimSpaceCoriolisAlpha"), ChaosImmediate_Evolution_SimSpaceCoriolisAlpha, TEXT("Settings for simulation space system for rigid body nodes"));
FAutoConsoleVariableRef CVarChaosImmPhysSimSpaceEulerAlpha(TEXT("p.Chaos.ImmPhys.SimSpaceEulerAlpha"), ChaosImmediate_Evolution_SimSpaceEulerAlpha, TEXT("Settings for simulation space system for rigid body nodes"));


Chaos::FRealSingle ChaosImmediate_Evolution_MinStepTime = 0.01f;
Chaos::FRealSingle ChaosImmediate_Evolution_FixedStepTime = -1.0f;
Chaos::FRealSingle ChaosImmediate_Evolution_FixedStepTolerance = 0.05f;
FAutoConsoleVariableRef CVarChaosImmPhysMinStepTime(TEXT("p.Chaos.ImmPhys.MinStepTime"), ChaosImmediate_Evolution_MinStepTime, TEXT("If non-zero, then if step time is lower than this, go into fixed step mode with this timestep."));
FAutoConsoleVariableRef CVarChaosImmPhysFixedStepTime(TEXT("p.Chaos.ImmPhys.FixedStepTime"), ChaosImmediate_Evolution_FixedStepTime, TEXT("Override fixed step time mode: fixed step time (if positive); variable time mode (if zero); asset defined (if negative)"));
FAutoConsoleVariableRef CVarChaosImmPhysFixedStepTolerance(TEXT("p.Chaos.ImmPhys.FixedStepTolerance"), ChaosImmediate_Evolution_FixedStepTolerance, TEXT("Tiem remainder required to add a new step (fraction of FixedStepTime)"));

int32 ChaosImmediate_Collision_Enabled = 1;
int32 ChaosImmediate_Collision_PairIterations = -1;
int32 ChaosImmediate_Collision_PushOutPairIterations = 0;	// Force disabled - not compatible with EConstraintSolverType::StandardPbd
int32 ChaosImmediate_Collision_Priority = 1;
Chaos::FRealSingle ChaosImmediate_Collision_CullDistance = 1.0f;
Chaos::FRealSingle ChaosImmediate_Collision_RestitutionThresholdMultiplier = 3.0f;
int32 ChaosImmediate_Collision_RestitutionEnabled = true;
int32 ChaosImmediate_Collision_DeferNarrowPhase = 1;
int32 ChaosImmediate_Collision_UseManifolds = 0;
FAutoConsoleVariableRef CVarChaosImmPhysCollisionDisable(TEXT("p.Chaos.ImmPhys.Collision.Enabled"), ChaosImmediate_Collision_Enabled, TEXT("Enable/Disable collisions in Immediate Physics."));
FAutoConsoleVariableRef CVarChaosImmPhysCollisionPairIterations(TEXT("p.Chaos.ImmPhys.Collision.PairIterations"), ChaosImmediate_Collision_PairIterations, TEXT("Override collision pair iterations (if >= 0)"));
FAutoConsoleVariableRef CVarChaosImmPhysCollisionPushOutPairIterations(TEXT("p.Chaos.ImmPhys.Collision.PushOutPairIterations"), ChaosImmediate_Collision_PushOutPairIterations, TEXT("Override collision push-out pair iterations (if >= 0)"));
FAutoConsoleVariableRef CVarChaosImmPhysCollisionPriority(TEXT("p.Chaos.ImmPhys.Collision.Priority"), ChaosImmediate_Collision_Priority, TEXT("Set the Collision constraint sort order (Joints have priority 0)"));
FAutoConsoleVariableRef CVarChaosImmPhysCollisionCullDistance(TEXT("p.Chaos.ImmPhys.Collision.CullDistance"), ChaosImmediate_Collision_CullDistance, TEXT("CullDistance"));
FAutoConsoleVariableRef CVarChaosImmPhysCollisionRestitutionThresholdMultiplier(TEXT("p.Chaos.ImmPhys.Collision.RestitutionThresholdMultiplier"), ChaosImmediate_Collision_RestitutionThresholdMultiplier, TEXT("Collision Restitution Threshold (Acceleration) = Multiplier * Gravity"));
FAutoConsoleVariableRef CVarChaosImmPhysCollisionRestitutionEnabled(TEXT("p.Chaos.ImmPhys.Collision.RestitutionEnabled"), ChaosImmediate_Collision_RestitutionEnabled, TEXT("Collision Restitution Enable/Disable"));
FAutoConsoleVariableRef CVarChaosImmPhysCollisionDeferNarrowPhase(TEXT("p.Chaos.ImmPhys.Collision.DeferNarrowPhase"), ChaosImmediate_Collision_DeferNarrowPhase, TEXT("Create contacts for all broadphase pairs, perform NarrowPhase later."));
FAutoConsoleVariableRef CVarChaosImmPhysCollisionUseManifolds(TEXT("p.Chaos.ImmPhys.Collision.UseManifolds"), ChaosImmediate_Collision_UseManifolds, TEXT("Enable/Disable use of manifoldes in collision."));

int32 ChaosImmediate_Joint_PairIterations = -1;
int32 ChaosImmediate_Joint_PushOutPairIterations = -1;
Chaos::FRealSingle ChaosImmediate_Joint_SwingTwistAngleTolerance = 1.0e-6f;
Chaos::FRealSingle ChaosImmediate_Joint_PositionTolerance = 0.025f;
Chaos::FRealSingle ChaosImmediate_Joint_AngleTolerance = 0.001f;
int32 ChaosImmediate_Joint_EnableTwistLimits = 1;
int32 ChaosImmediate_Joint_EnableSwingLimits = 1;
int32 ChaosImmediate_Joint_EnableDrives = 1;
Chaos::FRealSingle ChaosImmediate_Joint_LinearProjection = -1.0f;
Chaos::FRealSingle ChaosImmediate_Joint_AngularProjection = -1.0f;
Chaos::FRealSingle ChaosImmediate_Joint_Stiffness = -1.0f;
Chaos::FRealSingle ChaosImmediate_Joint_SoftLinearStiffness = -1.0f;
Chaos::FRealSingle ChaosImmediate_Joint_SoftTwistStiffness = -1.0f;
Chaos::FRealSingle ChaosImmediate_Joint_SoftTwistDamping = -1.0f;
Chaos::FRealSingle ChaosImmediate_Joint_SoftSwingStiffness = -1.0f;
Chaos::FRealSingle ChaosImmediate_Joint_SoftSwingDamping = -1.0f;
Chaos::FRealSingle ChaosImmediate_Joint_LinearDriveStiffness = -1.0f;
Chaos::FRealSingle ChaosImmediate_Joint_LinearDriveDamping = -1.0f;
Chaos::FRealSingle ChaosImmediate_Joint_AngularDriveStiffness = -1.0f;
Chaos::FRealSingle ChaosImmediate_Joint_AngularDriveDamping = -1.0f;
Chaos::FRealSingle ChaosImmediate_Joint_MinParentMassRatio = 0.2f;
Chaos::FRealSingle ChaosImmediate_Joint_MaxInertiaRatio = 5.0f;
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


//
// Select the solver technique to use until we settle on the final one...
//
int32 ChaosImmediate_SolverType = (int32)Chaos::EConstraintSolverType::StandardPbd;
FAutoConsoleVariableRef CVarChaosImmPhysSolverType(TEXT("p.Chaos.ImmPhys.SolverType"), ChaosImmediate_SolverType, TEXT("0 = None; 1 = GbfPbd; 2 = Pbd; 3 = QuasiPbd"));

//
// end remove when finished
//
//////////////////////////////////////////////////////////////////////////


// DebugDraw CVars
#if CHAOS_DEBUG_DRAW
int32 ChaosImmediate_DebugDrawParticles = 0;
int32 ChaosImmediate_DebugDrawShapes = 0;
int32 ChaosImmediate_DebugDrawShowStatics = 1;
int32 ChaosImmediate_DebugDrawShowKinematics = 1;
int32 ChaosImmediate_DebugDrawShowDynamics = 1;
int32 ChaosImmediate_DebugDrawBounds = 0;
int32 ChaosImmediate_DebugDrawCollisions = 0;
int32 ChaosImmediate_DebugDrawJoints = 0;
Chaos::DebugDraw::FChaosDebugDrawJointFeatures ChaosImmediate_DebugDrawJointFeatures = Chaos::DebugDraw::FChaosDebugDrawJointFeatures::MakeDefault();
int32 ChaosImmediate_DebugDrawSimulationSpace = 0;
FAutoConsoleVariableRef CVarChaosImmPhysDebugDrawParticles(TEXT("p.Chaos.ImmPhys.DebugDrawParticles"), ChaosImmediate_DebugDrawParticles, TEXT("Draw Particle Transforms (0 = never; 1 = end of frame; 2 = begin and end of frame; 3 = post-integate, post-apply and post-applypushout;)."));
FAutoConsoleVariableRef CVarChaosImmPhysDebugDrawShapes(TEXT("p.Chaos.ImmPhys.DebugDrawShapes"), ChaosImmediate_DebugDrawShapes, TEXT("Draw Shapes (0 = never; 1 = end of frame; 2 = begin and end of frame; 3 = post-integate, post-apply and post-applypushout;"));
FAutoConsoleVariableRef CVarChaosImmPhysDebugDrawShowStatics(TEXT("p.Chaos.ImmPhys.DebugDrawShowStatics"), ChaosImmediate_DebugDrawShowStatics, TEXT("Show statics if shape debug draw is enabled"));
FAutoConsoleVariableRef CVarChaosImmPhysDebugDrawShowKinematics(TEXT("p.Chaos.ImmPhys.DebugDrawShowKinematics"), ChaosImmediate_DebugDrawShowKinematics, TEXT("Show kinematics if shape debug draw is enabled"));
FAutoConsoleVariableRef CVarChaosImmPhysDebugDrawShowDynamics(TEXT("p.Chaos.ImmPhys.DebugDrawShowDynamics"), ChaosImmediate_DebugDrawShowDynamics, TEXT("Show dynamics if shape debug draw is enabled"));
FAutoConsoleVariableRef CVarChaosImmPhysDebugDrawBounds(TEXT("p.Chaos.ImmPhys.DebugDrawBounds"), ChaosImmediate_DebugDrawBounds, TEXT("Draw Particle Bounds (0 = never; 1 = end of frame; 2 = begin and end of frame; 3 = post-integate, post-apply and post-applypushout;)."));
FAutoConsoleVariableRef CVarChaosImmPhysDebugDrawCollisions(TEXT("p.Chaos.ImmPhys.DebugDrawCollisions"), ChaosImmediate_DebugDrawCollisions, TEXT("Draw Collisions (0 = never; 1 = end of frame; 2 = begin and end of frame; 3 = post-integate, post-apply and post-applypushout;)"));
FAutoConsoleVariableRef CVarChaosImmPhysDebugDrawJoints(TEXT("p.Chaos.ImmPhys.DebugDrawJoints"), ChaosImmediate_DebugDrawJoints, TEXT("Draw Joints. (0 = never; 1 = end of frame; 2 = begin and end of frame; 3 = post-integate, post-apply and post-applypushout; 4 = each Apply step)."));
FAutoConsoleVariableRef CVarChaosImmPhysDebugDrawJointFeaturesCoMConnector(TEXT("p.Chaos.ImmPhys.DebugDraw.JointFeatures.CoMConnector"), ChaosImmediate_DebugDrawJointFeatures.bCoMConnector, TEXT("Joint features mask (see FDebugDrawJointFeatures)."));
FAutoConsoleVariableRef CVarChaosImmPhysDebugDrawJointFeaturesActorConnector(TEXT("p.Chaos.ImmPhys.DebugDraw.JointFeatures.ActorConnector"), ChaosImmediate_DebugDrawJointFeatures.bActorConnector, TEXT("Joint features mask (see FDebugDrawJointFeatures)."));
FAutoConsoleVariableRef CVarChaosImmPhysDebugDrawJointFeaturesStretch(TEXT("p.Chaos.ImmPhys.DebugDraw.JointFeatures.Stretch"), ChaosImmediate_DebugDrawJointFeatures.bStretch, TEXT("Joint features mask (see FDebugDrawJointFeatures)."));
FAutoConsoleVariableRef CVarChaosImmPhysDebugDrawJointFeaturesAxes(TEXT("p.Chaos.ImmPhys.DebugDraw.JointFeatures.Axes"), ChaosImmediate_DebugDrawJointFeatures.bAxes, TEXT("Joint features mask (see FDebugDrawJointFeatures)."));
FAutoConsoleVariableRef CVarChaosImmPhysDebugDrawJointFeaturesLevel(TEXT("p.Chaos.ImmPhys.DebugDraw.JointFeatures.Level"), ChaosImmediate_DebugDrawJointFeatures.bLevel, TEXT("Joint features mask (see FDebugDrawJointFeatures)."));
FAutoConsoleVariableRef CVarChaosImmPhysDebugDrawJointFeaturesIndex(TEXT("p.Chaos.ImmPhys.DebugDraw.JointFeatures.Index"), ChaosImmediate_DebugDrawJointFeatures.bIndex, TEXT("Joint features mask (see FDebugDrawJointFeatures)."));
FAutoConsoleVariableRef CVarChaosImmPhysDebugDrawJointFeaturesColor(TEXT("p.Chaos.ImmPhys.DebugDraw.JointFeatures.Color"), ChaosImmediate_DebugDrawJointFeatures.bColor, TEXT("Joint features mask (see FDebugDrawJointFeatures)."));
FAutoConsoleVariableRef CVarChaosImmPhysDebugDrawJointFeaturesBatch(TEXT("p.Chaos.ImmPhys.DebugDraw.JointFeatures.Batch"), ChaosImmediate_DebugDrawJointFeatures.bBatch, TEXT("Joint features mask (see FDebugDrawJointFeatures)."));
FAutoConsoleVariableRef CVarChaosImmPhysDebugDrawJointFeaturesIsland(TEXT("p.Chaos.ImmPhys.DebugDraw.JointFeatures.Island"), ChaosImmediate_DebugDrawJointFeatures.bIsland, TEXT("Joint features mask (see FDebugDrawJointFeatures)."));
FAutoConsoleVariableRef CVarChaosImmPhysDebugDrawSimulationSpace(TEXT("p.Chaos.ImmPhys.DebugDrawSimulationSpace"), ChaosImmediate_DebugDrawSimulationSpace, TEXT("Draw the simulation frame of reference, acceleration and velocity."));

Chaos::DebugDraw::FChaosDebugDrawSettings ChaosImmPhysDebugDebugDrawSettings(
	/* ArrowSize =					*/ 1.5f,
	/* BodyAxisLen =				*/ 4.0f,
	/* ContactLen =					*/ 4.0f,
	/* ContactWidth =				*/ 2.0f,
	/* ContactPhiWidth =			*/ 0.0f,
	/* ContactOwnerWidth =			*/ 0.0f,
	/* ConstraintAxisLen =			*/ 5.0f,
	/* JointComSize =				*/ 2.0f,
	/* LineThickness =				*/ 0.15f,
	/* DrawScale =					*/ 1.0f,
	/* FontHeight =					*/ 10.0f,
	/* FontScale =					*/ 1.5f,
	/* ShapeThicknesScale =			*/ 1.0f,
	/* PointSize =					*/ 2.0f,
	/* VelScale =					*/ 0.0f,
	/* AngVelScale =				*/ 0.0f,
	/* ImpulseScale =				*/ 0.0f,
	/* DrawPriority =				*/ 10.0f,
	/* bShowSimple =				*/ true,
	/* bShowComplex =				*/ false,
	/* bInShowLevelSetCollision =	*/ false
	);

FAutoConsoleVariableRef CVarChaosImmPhysArrowSize(TEXT("p.Chaos.ImmPhys.DebugDraw.ArrowSize"), ChaosImmPhysDebugDebugDrawSettings.ArrowSize, TEXT("ArrowSize."));
FAutoConsoleVariableRef CVarChaosImmPhysBodyAxisLen(TEXT("p.Chaos.ImmPhys.DebugDraw.BodyAxisLen"), ChaosImmPhysDebugDebugDrawSettings.BodyAxisLen, TEXT("BodyAxisLen."));
FAutoConsoleVariableRef CVarChaosImmPhysContactLen(TEXT("p.Chaos.ImmPhys.DebugDraw.ContactLen"), ChaosImmPhysDebugDebugDrawSettings.ContactLen, TEXT("ContactLen."));
FAutoConsoleVariableRef CVarChaosImmPhysContactWidth(TEXT("p.Chaos.ImmPhys.DebugDraw.ContactWidth"), ChaosImmPhysDebugDebugDrawSettings.ContactWidth, TEXT("ContactWidth."));
FAutoConsoleVariableRef CVarChaosImmPhysContactPhiWidth(TEXT("p.Chaos.ImmPhys.DebugDraw.ContactPhiWidth"), ChaosImmPhysDebugDebugDrawSettings.ContactPhiWidth, TEXT("ContactPhiWidth."));
FAutoConsoleVariableRef CVarChaosImmPhysContactOwnerWidth(TEXT("p.Chaos.ImmPhys.DebugDraw.ContactOwnerWidth"), ChaosImmPhysDebugDebugDrawSettings.ContactOwnerWidth, TEXT("ContactOwnerWidth."));
FAutoConsoleVariableRef CVarChaosImmPhysConstraintAxisLen(TEXT("p.Chaos.ImmPhys.DebugDraw.ConstraintAxisLen"), ChaosImmPhysDebugDebugDrawSettings.ConstraintAxisLen, TEXT("ConstraintAxisLen."));
FAutoConsoleVariableRef CVarChaosImmPhysLineThickness(TEXT("p.Chaos.ImmPhys.DebugDraw.LineThickness"), ChaosImmPhysDebugDebugDrawSettings.LineThickness, TEXT("LineThickness."));
FAutoConsoleVariableRef CVarChaosImmPhysLineShapeThickness(TEXT("p.Chaos.ImmPhys.DebugDraw.ShapeLineThicknessScale"), ChaosImmPhysDebugDebugDrawSettings.ShapeThicknesScale, TEXT("Shape lineThickness multiplier."));
FAutoConsoleVariableRef CVarChaosImmPhysVelScale(TEXT("p.Chaos.ImmPhys.DebugDraw.VelScale"), ChaosImmPhysDebugDebugDrawSettings.VelScale, TEXT("If >0 show velocity when drawing particle transforms."));
FAutoConsoleVariableRef CVarChaosImmPhysAngVelScale(TEXT("p.Chaos.ImmPhys.DebugDraw.AngVelScale"), ChaosImmPhysDebugDebugDrawSettings.AngVelScale, TEXT("If >0 show angular velocity when drawing particle transforms."));
FAutoConsoleVariableRef CVarChaosImmPhysImpulseScale(TEXT("p.Chaos.ImmPhys.DebugDraw.ImpulseScale"), ChaosImmPhysDebugDebugDrawSettings.ImpulseScale, TEXT("If >0 show impulses when drawing collisions."));
FAutoConsoleVariableRef CVarChaosImmPhysScale(TEXT("p.Chaos.ImmPhys.DebugDraw.Scale"), ChaosImmPhysDebugDebugDrawSettings.DrawScale, TEXT("Scale applied to all Chaos Debug Draw line lengths etc."));
#endif

namespace ImmediatePhysics_Chaos
{
	struct FSimulation::FImplementation
	{
	public:
		using FCollisionConstraints = Chaos::FPBDCollisionConstraints;
		using FCollisionDetector = Chaos::FParticlePairCollisionDetector;
		using FParticleHandle = Chaos::FGeometryParticleHandle;
		using FParticlePair = Chaos::TVec2<Chaos::FGeometryParticleHandle*>;
		using FRigidParticleSOAs = Chaos::FPBDRigidsSOAs;

		FImplementation()
			: Particles()
			, Joints()
			, Collisions(Particles, CollidedParticles, ParticleMaterials, PerParticleMaterials, 0, 0, ChaosImmediate_Collision_CullDistance)
			, BroadPhase(&ActivePotentiallyCollidingPairs, nullptr, nullptr, ChaosImmediate_Collision_CullDistance)
			, NarrowPhase()
			, CollisionDetector(BroadPhase, NarrowPhase, Collisions)
			, JointsRule(0, Joints)
			, CollisionsRule(1, Collisions)
			, Evolution(Particles, ParticlePrevXs, ParticlePrevRs, CollisionDetector, ChaosImmediate_Evolution_BoundsExtension)
			, NumActiveDynamicActorHandles(0)
			, SimulationSpace()
			, RollingAverageStepTime(ChaosImmediate_Evolution_InitialStepTime)
			, NumRollingAverageStepTimes(1)
			, MaxNumRollingAverageStepTimes(ChaosImmediate_Evolution_DeltaTimeCount)
			, bActorsDirty(false)
		{
			Particles.GetParticleHandles().AddArray(&CollidedParticles);
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
		TArray<FActorHandle*> StaticParticles;
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

		Chaos::FSimulationSpace SimulationSpace;

		Chaos::FReal FixedStepTime;
		Chaos::FReal RollingAverageStepTime;
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
		Implementation->Collisions.SetSolverType(EConstraintSolverType::StandardPbd);
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
			[this](const FReal Dt, const TArray<FPBDCollisionConstraintHandle*>& InConstraintHandles)
			{
				if (ChaosImmediate_DebugDrawCollisions == 4)
				{
					DebugDraw::DrawCollisions(Implementation->SimulationSpace.Transform, Implementation->Collisions, 0.3f, &ChaosImmPhysDebugDebugDrawSettings);
				}
				DebugDrawDynamicParticles(4, 4, FColor(128, 128, 0));
			});
		Implementation->Collisions.SetPostApplyPushOutCallback(
			[this](const FReal Dt, const TArray<FPBDCollisionConstraintHandle*>& InConstraintHandles, bool bRequiresAnotherIteration)
			{
				if (ChaosImmediate_DebugDrawCollisions == 4)
				{
					DebugDraw::DrawCollisions(Implementation->SimulationSpace.Transform, Implementation->Collisions, 0.6f, &ChaosImmPhysDebugDebugDrawSettings);
				}
			});
		Implementation->Joints.SetPreApplyCallback(
			[this](const FReal Dt, const TArray<FPBDJointConstraintHandle*>& InConstraintHandles)
			{
				if (ChaosImmediate_DebugDrawJoints == 4)
				{
					DebugDraw::DrawJointConstraints(Implementation->SimulationSpace.Transform, InConstraintHandles, 0.3f, DebugDraw::FChaosDebugDrawJointFeatures::MakeDefault(), &ChaosImmPhysDebugDebugDrawSettings);
				}
			});
		Implementation->Joints.SetPostApplyCallback(
			[this](const FReal Dt, const TArray<FPBDJointConstraintHandle*>& InConstraintHandles)
			{
				if (ChaosImmediate_DebugDrawJoints == 4)
				{
					DebugDraw::DrawJointConstraints(Implementation->SimulationSpace.Transform, InConstraintHandles, 0.6f, DebugDraw::FChaosDebugDrawJointFeatures::MakeDefault(), &ChaosImmPhysDebugDebugDrawSettings);
				}
				DebugDrawDynamicParticles(4, 4, FColor(0, 128, 0));
			});
		Implementation->Joints.SetPostProjectCallback(
			[this](const FReal Dt, const TArray<FPBDJointConstraintHandle*>& InConstraintHandles)
			{
				if (ChaosImmediate_DebugDrawJoints == 4)
				{
					DebugDraw::DrawJointConstraints(Implementation->SimulationSpace.Transform, InConstraintHandles, 0.6f, DebugDraw::FChaosDebugDrawJointFeatures::MakeDefault(), &ChaosImmPhysDebugDebugDrawSettings);
				}
			});
#endif
	}

	FSimulation::~FSimulation()
	{
		using namespace Chaos;

		// NOTE: Particles now hold a list of all the constraints that reference them, but when
		// we delete a particle, we do not notify the constraints. When we destroy constarints
		// it tries to remove itself from the particle's list, so we must destroy the
		// constraint first.

		for (FJointHandle* JointHandle : Implementation->JointHandles)
		{
			delete JointHandle;
		}
		Implementation->JointHandles.Empty();

		for (FActorHandle* ActorHandle : Implementation->ActorHandles)
		{
			delete ActorHandle;
		}
		Implementation->ActorHandles.Empty();
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
			// @todo(ccaulfield): We cannot ask for the physical material on a task thread, because FMICReentranceGuard in UMaterialInstance will assert (in editor). Fix this...
			// For now we just use material defaults when actors are created on a task thread. This happens when adding world-objects to a RigidBody AnimNode simulation.
			if (IsInGameThread())
			{
				UPhysicalMaterial* SimplePhysMat = BodyInstance->GetSimplePhysicalMaterial();
				if (SimplePhysMat != nullptr)
				{
					Material->Friction = SimplePhysMat->Friction;
					Material->Restitution = SimplePhysMat->Restitution;
				}
			}
		}

		ActorHandle->GetParticle()->AuxilaryValue(Implementation->ParticleMaterials) = MakeSerializable(Material);
		ActorHandle->GetParticle()->AuxilaryValue(Implementation->PerParticleMaterials) = MoveTemp(Material);
		ActorHandle->GetParticle()->AuxilaryValue(Implementation->CollidedParticles) = false;
		ActorHandle->GetParticle()->AuxilaryValue(Implementation->ParticlePrevXs) = ActorHandle->GetParticle()->X();
		ActorHandle->GetParticle()->AuxilaryValue(Implementation->ParticlePrevRs) = ActorHandle->GetParticle()->R();

		Implementation->bActorsDirty = true;

		return ActorHandle;
	}

	void FSimulation::DestroyActor(FActorHandle* ActorHandle)
	{
		// @todo(ccaulfield): FActorHandle could remember its index to optimize this

		RemoveFromCollidingPairs(ActorHandle);

		// If any joints reference the particle, we must destroy them
		TArray<FJointHandle*> ActorJointHandles;
		for (FJointHandle* JointHandle : Implementation->JointHandles)
		{
			if (JointHandle != nullptr)
			{
				if ((JointHandle->GetActorHandles()[0] == ActorHandle) || (JointHandle->GetActorHandles()[1] == ActorHandle))
				{
					ActorJointHandles.Add(JointHandle);
				}
			}
		}
		for (FJointHandle* JointHandle : ActorJointHandles)
		{
			DestroyJoint(JointHandle);
		}
		ActorJointHandles.Empty();


		int32 Index = Implementation->ActorHandles.Remove(ActorHandle);
		delete ActorHandle;

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

	// Currently sets up potential collision with ActorHandle and all dynamics
	void FSimulation::AddToCollidingPairs(FActorHandle* ActorHandle)
	{
		using namespace Chaos;
		FGeometryParticleHandle* Particle0 = ActorHandle->GetParticle();
		for (FActorHandle* OtherActorHandle : Implementation->ActorHandles)
		{
			FGeometryParticleHandle* Particle1 = OtherActorHandle->GetParticle();
			if ((OtherActorHandle != ActorHandle) && OtherActorHandle->IsSimulated())
			{
				Implementation->PotentiallyCollidingPairs.Emplace(typename FImplementation::FParticlePair(Particle0, Particle1));
			}
		}
		Implementation->bActorsDirty = true;
	}

	void FSimulation::RemoveFromCollidingPairs(FActorHandle* ActorHandle)
	{
		for (typename FImplementation::FParticlePair& ParticlePair : Implementation->PotentiallyCollidingPairs)
		{
			if ((ParticlePair[0] == ActorHandle->GetParticle()) || (ParticlePair[1] == ActorHandle->GetParticle()))
			{
				ParticlePair[0] = nullptr;
				ParticlePair[1] = nullptr;
			}
		}
		Implementation->bActorsDirty = true;
	}

	void FSimulation::PackCollidingPairs()
	{
		int32 NextValidPairIndex = 0;
		for (int32 PairIndex = 0; PairIndex < Implementation->PotentiallyCollidingPairs.Num(); ++PairIndex)
		{
			if (Implementation->PotentiallyCollidingPairs[PairIndex][0] == nullptr)
			{
				NextValidPairIndex = FMath::Max(NextValidPairIndex, PairIndex + 1);
				while ((NextValidPairIndex < Implementation->PotentiallyCollidingPairs.Num()) && (Implementation->PotentiallyCollidingPairs[NextValidPairIndex][0] == nullptr))
				{
					++NextValidPairIndex;
				}
				if (NextValidPairIndex >= Implementation->PotentiallyCollidingPairs.Num())
				{
					Implementation->PotentiallyCollidingPairs.SetNum(PairIndex);
					break;
				}
				Implementation->PotentiallyCollidingPairs[PairIndex] = Implementation->PotentiallyCollidingPairs[NextValidPairIndex];
				Implementation->PotentiallyCollidingPairs[NextValidPairIndex][0] = nullptr;
				Implementation->PotentiallyCollidingPairs[NextValidPairIndex][1] = nullptr;
				++NextValidPairIndex;
			}
		}
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

			const FGeometryParticleHandle* ParticleA = IgnorePair.A->GetParticle();
			const FGeometryParticleHandle* ParticleB = IgnorePair.B->GetParticle();

			Implementation->IgnoreCollisionParticlePairTable.FindOrAdd(ParticleA).Add(ParticleB); 
			Implementation->IgnoreCollisionParticlePairTable.FindOrAdd(ParticleB).Add(ParticleA);
		}

		Implementation->PotentiallyCollidingPairs.Empty();
		int NumActorHandles = Implementation->ActorHandles.Num();
		for (int ActorHandleIndex0 = 0; ActorHandleIndex0 < NumActorHandles; ++ActorHandleIndex0)
		{
			FActorHandle* ActorHandle0 = Implementation->ActorHandles[ActorHandleIndex0];
			FGeometryParticleHandle* Particle0 = ActorHandle0->GetParticle();
			//FPBDRigidParticleHandle* Rigid0 = Particle0->CastToRigidParticle();
			//if (Rigid0 && (Rigid0->ObjectState() == EObjectStateType::Dynamic))
			{
				for (int ActorHandleIndex1 = ActorHandleIndex0 + 1; ActorHandleIndex1 < NumActorHandles; ++ActorHandleIndex1)
				{
					FActorHandle* ActorHandle1 = Implementation->ActorHandles[ActorHandleIndex1];
					FGeometryParticleHandle* Particle1 = ActorHandle1->GetParticle();

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
			FPBDRigidParticleHandle* Particle = ActorHandle->GetParticle()->CastToRigidParticle();
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
			if ((ParticlePair[0] != nullptr) && (ParticlePair[1] != nullptr))
			{
				bool bAnyDisabled = FGenericParticleHandle(ParticlePair[0])->Disabled() || FGenericParticleHandle(ParticlePair[1])->Disabled();
				bool bAnyDynamic = FGenericParticleHandle(ParticlePair[0])->IsDynamic() || FGenericParticleHandle(ParticlePair[1])->IsDynamic();
				if (bAnyDynamic && !bAnyDisabled)
				{
					Implementation->ActivePotentiallyCollidingPairs.Add(ParticlePair);
				}
			}
		}
	}

	void FSimulation::InitSimulationSpace(
		const FTransform& Transform)
	{
		UpdateSimulationSpace(Transform, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector);
	}

	void FSimulation::UpdateSimulationSpace(
		const FTransform& Transform,
		const FVector& LinearVel,
		const FVector& AngularVel,
		const FVector& LinearAcc,
		const FVector& AngularAcc)
	{
		Implementation->SimulationSpace.Transform = Transform;
		Implementation->SimulationSpace.LinearAcceleration = LinearAcc;
		Implementation->SimulationSpace.AngularAcceleration = AngularAcc;
		Implementation->SimulationSpace.LinearVelocity = LinearVel;
		Implementation->SimulationSpace.AngularVelocity = AngularVel;
	}

	void FSimulation::SetSimulationSpaceSettings(const FReal MasterAlpha, const FVector& ExternalLinearEtherDrag)
	{
		using namespace Chaos;

		FSimulationSpaceSettings& SimSpaceSettings = Implementation->Evolution.GetSimulationSpaceSettings();
		SimSpaceSettings.MasterAlpha = MasterAlpha;
		SimSpaceSettings.ExternalLinearEtherDrag = ExternalLinearEtherDrag;
		SimSpaceSettings.CentrifugalAlpha = ChaosImmediate_Evolution_SimSpaceCentrifugalAlpha;
		SimSpaceSettings.CoriolisAlpha = ChaosImmediate_Evolution_SimSpaceCoriolisAlpha;
		SimSpaceSettings.EulerAlpha = ChaosImmediate_Evolution_SimSpaceEulerAlpha;
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

	FReal FSimulation::UpdateStepTime(const FReal DeltaTime, const FReal MaxStepTime)
	{
		Implementation->RollingAverageStepTime = Implementation->RollingAverageStepTime + (DeltaTime - Implementation->RollingAverageStepTime) / Implementation->NumRollingAverageStepTimes;
		Implementation->RollingAverageStepTime = FMath::Min(Implementation->RollingAverageStepTime, MaxStepTime);
		Implementation->NumRollingAverageStepTimes = FMath::Min(Implementation->NumRollingAverageStepTimes + 1, Implementation->MaxNumRollingAverageStepTimes);
		return Implementation->RollingAverageStepTime;
	}

	void FSimulation::Simulate(FReal InDeltaTime, FReal MaxStepTime, int32 MaxSubSteps, const FVector& InGravity)
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
			JointsSettings.bEnableTwistLimits = ChaosImmediate_Joint_EnableTwistLimits != 0;
			JointsSettings.bEnableSwingLimits = ChaosImmediate_Joint_EnableSwingLimits != 0;
			JointsSettings.bEnableDrives = ChaosImmediate_Joint_EnableDrives != 0;
			JointsSettings.LinearStiffnessOverride = ChaosImmediate_Joint_Stiffness;
			JointsSettings.TwistStiffnessOverride = ChaosImmediate_Joint_Stiffness;
			JointsSettings.SwingStiffnessOverride = ChaosImmediate_Joint_Stiffness;
			JointsSettings.LinearProjectionOverride = ChaosImmediate_Joint_LinearProjection;
			JointsSettings.AngularProjectionOverride = ChaosImmediate_Joint_AngularProjection;
			JointsSettings.SoftLinearStiffnessOverride = ChaosImmediate_Joint_SoftLinearStiffness;
			JointsSettings.SoftTwistStiffnessOverride = ChaosImmediate_Joint_SoftTwistStiffness;
			JointsSettings.SoftTwistDampingOverride = ChaosImmediate_Joint_SoftTwistDamping;
			JointsSettings.SoftSwingStiffnessOverride = ChaosImmediate_Joint_SoftSwingStiffness;
			JointsSettings.SoftSwingDampingOverride = ChaosImmediate_Joint_SoftSwingDamping;
			JointsSettings.LinearDriveStiffnessOverride = ChaosImmediate_Joint_LinearDriveStiffness;
			JointsSettings.LinearDriveDampingOverride = ChaosImmediate_Joint_LinearDriveDamping;
			JointsSettings.AngularDriveStiffnessOverride = ChaosImmediate_Joint_AngularDriveStiffness;
			JointsSettings.AngularDriveDampingOverride = ChaosImmediate_Joint_AngularDriveDamping;
			Implementation->Joints.SetSettings(JointsSettings);

			Implementation->Collisions.SetRestitutionEnabled(ChaosImmediate_Collision_RestitutionEnabled != 0);
			Implementation->Collisions.SetRestitutionThreshold(ChaosImmediate_Collision_RestitutionThresholdMultiplier * InGravity.Size());
			Implementation->Collisions.SetCollisionsEnabled(ChaosImmediate_Collision_Enabled != 0);
			Implementation->CollisionsRule.SetPriority(ChaosImmediate_Collision_Priority);

			Implementation->BroadPhase.SetCullDustance(ChaosImmediate_Collision_CullDistance);
			Implementation->Evolution.SetBoundsExtension(ChaosImmediate_Evolution_BoundsExtension);

			Implementation->NarrowPhase.GetContext().bDeferUpdate = (ChaosImmediate_Collision_DeferNarrowPhase != 0);
			Implementation->NarrowPhase.GetContext().bAllowManifolds = (ChaosImmediate_Collision_UseManifolds != 0);

			Implementation->Collisions.SetSolverType((EConstraintSolverType)ChaosImmediate_SolverType);
			// @todo(chaos): implement solver type switching for joints
			//Implementation->Joints.SetSolverType((EConstraintSolverType)ChaosImmediate_SolverType);

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
			if (RemainderTime > ChaosImmediate_Evolution_FixedStepTolerance * StepTime)
			{
				++NumSteps;
				RewindTime = StepTime - RemainderTime;
			}
			NumSteps = FMath::Max(1, NumSteps);
		}

		// Handle new or deleted particles
		if (Implementation->bActorsDirty)
		{
			PackCollidingPairs();
			UpdateActivePotentiallyCollidingPairs();
			Implementation->bActorsDirty = false;
		}

		UE_LOG(LogChaosJoint, Verbose, TEXT("Simulate Dt = %f Steps %d x %f (Rewind %f)"), DeltaTime, NumSteps, StepTime, RewindTime);
		Implementation->Evolution.SetGravity(InGravity);
		Implementation->Evolution.SetSimulationSpace(Implementation->SimulationSpace);
		Implementation->Evolution.Advance(StepTime, NumSteps, RewindTime);

		DebugDrawStaticParticles(1, 4, FColor(128, 0, 0));
		DebugDrawKinematicParticles(1, 4, FColor(64, 32, 0));
		DebugDrawDynamicParticles(1, 3, FColor(255, 255, 0));
		DebugDrawConstraints(1, 2, 1.0f);
		DebugDrawSimulationSpace();
	}

	void FSimulation::DebugDrawStaticParticles(const int32 MinDebugLevel, const int32 MaxDebugLevel, const FColor& Color)
	{
#if CHAOS_DEBUG_DRAW
		using namespace Chaos;
		if (FDebugDrawQueue::IsDebugDrawingEnabled())
		{
			if (!ChaosImmediate_DebugDrawShowStatics)
			{
				return;
			}
			if ((ChaosImmediate_DebugDrawParticles >= MinDebugLevel) && (ChaosImmediate_DebugDrawParticles <= MaxDebugLevel))
			{
				DebugDraw::DrawParticleTransforms(Implementation->SimulationSpace.Transform, Implementation->Particles.GetActiveStaticParticlesView(), &ChaosImmPhysDebugDebugDrawSettings);
			}
			if ((ChaosImmediate_DebugDrawShapes >= MinDebugLevel) && (ChaosImmediate_DebugDrawShapes <= MaxDebugLevel))
			{
				DebugDraw::DrawParticleShapes(Implementation->SimulationSpace.Transform, Implementation->Particles.GetActiveStaticParticlesView(), Color, &ChaosImmPhysDebugDebugDrawSettings);
			}
			if ((ChaosImmediate_DebugDrawBounds >= MinDebugLevel) && (ChaosImmediate_DebugDrawBounds <= MaxDebugLevel))
			{
				DebugDraw::DrawParticleBounds(Implementation->SimulationSpace.Transform, Implementation->Particles.GetActiveStaticParticlesView(), 0.0f, 0.0f, 0.0f, &ChaosImmPhysDebugDebugDrawSettings);
			}
		}
#endif
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
				DebugDraw::DrawParticleTransforms(Implementation->SimulationSpace.Transform, Implementation->Particles.GetActiveKinematicParticlesView(), &ChaosImmPhysDebugDebugDrawSettings);
			}
			if ((ChaosImmediate_DebugDrawShapes >= MinDebugLevel) && (ChaosImmediate_DebugDrawShapes <= MaxDebugLevel))
			{
				DebugDraw::DrawParticleShapes(Implementation->SimulationSpace.Transform, Implementation->Particles.GetActiveKinematicParticlesView(), Color, &ChaosImmPhysDebugDebugDrawSettings);
			}
			if ((ChaosImmediate_DebugDrawBounds >= MinDebugLevel) && (ChaosImmediate_DebugDrawBounds <= MaxDebugLevel))
			{
				DebugDraw::DrawParticleBounds(Implementation->SimulationSpace.Transform, Implementation->Particles.GetActiveKinematicParticlesView(), 0.0f, 0.0f, 0.0f, &ChaosImmPhysDebugDebugDrawSettings);
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
				DebugDraw::DrawParticleTransforms(Implementation->SimulationSpace.Transform, Implementation->Particles.GetActiveParticlesView(), &ChaosImmPhysDebugDebugDrawSettings);
			}
			if ((ChaosImmediate_DebugDrawShapes >= MinDebugLevel) && (ChaosImmediate_DebugDrawShapes <= MaxDebugLevel))
			{
				DebugDraw::DrawParticleShapes(Implementation->SimulationSpace.Transform, Implementation->Particles.GetActiveParticlesView(), Color, &ChaosImmPhysDebugDebugDrawSettings);
			}
			if ((ChaosImmediate_DebugDrawBounds >= MinDebugLevel) && (ChaosImmediate_DebugDrawBounds <= MaxDebugLevel))
			{
				DebugDraw::DrawParticleBounds(Implementation->SimulationSpace.Transform, Implementation->Particles.GetActiveParticlesView(), 0.0f, 0.0f, 0.0f, &ChaosImmPhysDebugDebugDrawSettings);
			}
		}
#endif
	}

	void FSimulation::DebugDrawConstraints(const int32 MinDebugLevel, const int32 MaxDebugLevel, const FRealSingle ColorScale)
	{
#if CHAOS_DEBUG_DRAW
		using namespace Chaos;
		if (FDebugDrawQueue::IsDebugDrawingEnabled())
		{
			if ((ChaosImmediate_DebugDrawCollisions >= MinDebugLevel) && (ChaosImmediate_DebugDrawCollisions <= MaxDebugLevel))
			{
				DebugDraw::DrawCollisions(Implementation->SimulationSpace.Transform, Implementation->Collisions, ColorScale, &ChaosImmPhysDebugDebugDrawSettings);
			}
			if ((ChaosImmediate_DebugDrawJoints >= MinDebugLevel) && (ChaosImmediate_DebugDrawJoints <= MaxDebugLevel))
			{
				DebugDraw::DrawJointConstraints(Implementation->SimulationSpace.Transform, Implementation->Joints, ColorScale, ChaosImmediate_DebugDrawJointFeatures, &ChaosImmPhysDebugDebugDrawSettings);
			}
		}
#endif
	}

	void FSimulation::DebugDrawSimulationSpace()
	{
#if CHAOS_DEBUG_DRAW
		using namespace Chaos;
		if (FDebugDrawQueue::IsDebugDrawingEnabled())
		{
			if (ChaosImmediate_DebugDrawSimulationSpace)
			{
				DebugDraw::DrawSimulationSpace(Implementation->SimulationSpace, &ChaosImmPhysDebugDebugDrawSettings);
			}
		}
#endif
	}
}
