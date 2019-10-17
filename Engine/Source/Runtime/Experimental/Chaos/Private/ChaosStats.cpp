// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ChaosStats.h"

DEFINE_STAT(STAT_PhysicsAdvance);
DEFINE_STAT(STAT_SolverAdvance);
DEFINE_STAT(STAT_HandleSolverCommands);
DEFINE_STAT(STAT_IntegrateSolver);
DEFINE_STAT(STAT_SyncProxies);
DEFINE_STAT(STAT_PhysCommands);
DEFINE_STAT(STAT_TaskCommands);
DEFINE_STAT(STAT_KinematicUpdate);
DEFINE_STAT(STAT_BeginFrame);
DEFINE_STAT(STAT_EndFrame);
DEFINE_STAT(STAT_UpdateReverseMapping);
DEFINE_STAT(STAT_CollisionContactsCallback);
DEFINE_STAT(STAT_BreakingCallback);
DEFINE_STAT(STAT_TrailingCallback);
DEFINE_STAT(STAT_GCRaycast);
DEFINE_STAT(STAT_GCOverlap);
DEFINE_STAT(STAT_GCSweep);
DEFINE_STAT(STAT_GCCUpdateBounds);
DEFINE_STAT(STAT_GCCUGlobalMatrices);
DEFINE_STAT(STAT_GCInitDynamicData);
DEFINE_STAT(STAT_LockWaits);
DEFINE_STAT(STAT_GeomBeginFrame);
DEFINE_STAT(STAT_SkelMeshUpdateAnim);
DEFINE_STAT(STAT_DispatchEventNotifies);
DEFINE_STAT(STAT_DispatchCollisionEvents);
DEFINE_STAT(STAT_DispatchBreakEvents);
DEFINE_STAT(STAT_BufferPhysicsResults);
DEFINE_STAT(STAT_FlipResults);
DEFINE_STAT(STAT_CacheResultGeomCollection);
DEFINE_STAT(STAT_CacheResultStaticMesh);
DEFINE_STAT(STAT_CaptureDisabledState);
DEFINE_STAT(STAT_CalcGlobalGCMatrices);
DEFINE_STAT(STAT_CalcGlobalGCBounds);
DEFINE_STAT(STAT_CalcParticleToWorld);
DEFINE_STAT(STAT_CreateBodies);
DEFINE_STAT(STAT_UpdateParams);
DEFINE_STAT(STAT_DisableCollisions);
DEFINE_STAT(STAT_EvolutionAndKinematicUpdate);
DEFINE_STAT(STAT_AdvanceEventWaits);
DEFINE_STAT(STAT_ResetCollisionRule);
DEFINE_STAT(STAT_EventDataGathering)
DEFINE_STAT(STAT_FillProducerData)
DEFINE_STAT(STAT_FlipBuffersIfRequired)
DEFINE_STAT(STAT_GatherCollisionEvent)
DEFINE_STAT(STAT_GatherBreakingEvent)
DEFINE_STAT(STAT_GatherTrailingEvent)
DEFINE_STAT(STAT_GatherSleepingEvent)

DEFINE_STAT(STAT_ParamUpdateObject);
DEFINE_STAT(STAT_ParamUpdateField);
DEFINE_STAT(STAT_SyncEvents_GameThread);

DEFINE_STAT(STAT_PhysicsStatUpdate);
DEFINE_STAT(STAT_PhysicsThreadTime);
DEFINE_STAT(STAT_PhysicsThreadTimeEff);
DEFINE_STAT(STAT_PhysicsThreadFps);
DEFINE_STAT(STAT_PhysicsThreadFpsEff);

DEFINE_STAT(STAT_Scene_StartFrame);
DEFINE_STAT(STAT_Scene_EndFrame);

DEFINE_STAT(STAT_ParamUpdateField_Object);
DEFINE_STAT(STAT_ParamUpdateField_DynamicState);
DEFINE_STAT(STAT_ParamUpdateField_ExternalClusterStrain);
DEFINE_STAT(STAT_ParamUpdateField_Kill);
DEFINE_STAT(STAT_ParamUpdateField_LinearVelocity);
DEFINE_STAT(STAT_ParamUpdateField_AngularVelocity);
DEFINE_STAT(STAT_ParamUpdateField_SleepingThreshold);
DEFINE_STAT(STAT_ParamUpdateField_DisableThreshold);
DEFINE_STAT(STAT_ParamUpdateField_InternalClusterStrain);
DEFINE_STAT(STAT_ParamUpdateField_PositionStatic);
DEFINE_STAT(STAT_ParamUpdateField_PositionTarget);
DEFINE_STAT(STAT_ParamUpdateField_PositionAnimated);
DEFINE_STAT(STAT_ParamUpdateField_DynamicConstraint);

// Field update stats