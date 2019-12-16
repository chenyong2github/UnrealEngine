// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#if WITH_PHYSX
#include "PhysXPublic.h"
#endif
#include "Physics/PhysicsInterfaceDeclares.h"
#include "Physics/PhysicsInterfaceCore.h"
#include "PhysicsInterfaceDeclaresCore.h"

#include "PhysicsEngine/CollisionQueryFilterCallback.h"
#include "PhysicsCore.h"
#if PHYSICS_INTERFACE_PHYSX
#include "PhysXInterfaceWrapper.h"
#endif

#include "PhysTestSerializer.h"

#include "SQAccelerator.h"
#include "SQVerifier.h"
#include "PBDRigidsSolver.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"

int32 ForceStandardSQ = 0;
FAutoConsoleVariableRef CVarForceStandardSQ(TEXT("p.ForceStandardSQ"), ForceStandardSQ, TEXT("If enabled, we force the standard scene query even if custom SQ structure is enabled"));


#if !UE_BUILD_SHIPPING
int32 SerializeSQs = 0;
int32 SerializeBadSQs = 0;
int32 ReplaySQs = 0;

FAutoConsoleVariableRef CVarSerializeSQs(TEXT("p.SerializeSQs"), SerializeSQs, TEXT("If enabled, we create a sq capture per sq. This can be very expensive as the entire scene is saved out"));
FAutoConsoleVariableRef CVarReplaySweeps(TEXT("p.ReplaySQs"), ReplaySQs, TEXT("If enabled, we rerun the sq against chaos"));
FAutoConsoleVariableRef CVarSerializeBadSweeps(TEXT("p.SerializeBadSQs"), SerializeBadSQs, TEXT("If enabled, we create a sq capture whenever chaos and physx diverge"));

void FinalizeCapture(FPhysTestSerializer& Serializer)
{
	if (SerializeSQs)
	{
		Serializer.Serialize(TEXT("SQCapture"));
	}
#if WITH_PHYSX
	if (ReplaySQs)
	{
		const bool bReplaySuccess = SQComparisonHelper(Serializer);
		if (!bReplaySuccess)
		{
			UE_LOG(LogPhysicsCore, Warning, TEXT("Chaos SQ does not match physx"));
			if (SerializeBadSQs && !SerializeSQs)
			{
				Serializer.Serialize(TEXT("BadSQCapture"));
			}
		}
	}
#endif
}
#else
constexpr int32 SerializeSQs = 0;
constexpr int32 ReplaySQs = 0;
// No-op in shipping
void FinalizeCapture(FPhysTestSerializer& Serializer) {}
#endif

void LowLevelRaycast(FPhysScene& Scene, const FVector& Start, const FVector& Dir, float DeltaMag, FPhysicsHitCallback<FHitRaycast>& HitBuffer, EHitFlags OutputFlags, FQueryFlags QueryFlags, const FCollisionFilterData& Filter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase* QueryCallback, const FQueryDebugParams& DebugParams)
{
#if !defined(PHYSICS_INTERFACE_PHYSX) || !PHYSICS_INTERFACE_PHYSX
	if (const auto& SolverAccelerationStructure = Scene.GetScene().GetSpacialAcceleration())
	{
		FChaosSQAccelerator SQAccelerator(*SolverAccelerationStructure);
		double Time = 0.0;
		{
			FScopedDurationTimer Timer(Time);
			SQAccelerator.Raycast(Start, Dir, DeltaMag, HitBuffer, OutputFlags, QueryFilterData, *QueryCallback, DebugParams);
		}

		/*if (((SerializeSQs && Time * 1000.0 * 1000.0 > SerializeSQs) || (false)) && IsInGameThread())
		{
			FPhysTestSerializer Serializer;
			Serializer.SetPhysicsData(*Scene.GetSolver()->GetEvolution());
			FSQCapture& RaycastCapture = Serializer.CaptureSQ();
			RaycastCapture.StartCaptureChaosRaycast(*Scene.GetSolver()->GetEvolution(), Start, Dir, DeltaMag, OutputFlags, QueryFilterData, Filter, *QueryCallback);
			RaycastCapture.EndCaptureChaosRaycast(HitBuffer);

			FinalizeCapture(Serializer);
		}*/
	}
#else
	if (SerializeSQs | ReplaySQs)
	{
		FPhysTestSerializer Serializer;
		Serializer.SetPhysicsData(*Scene.GetPxScene());
		FSQCapture& SweepCapture = Serializer.CaptureSQ();
		SweepCapture.StartCapturePhysXRaycast(*Scene.GetPxScene(), Start, Dir, DeltaMag, OutputFlags, QueryFilterData, Filter, *QueryCallback);
		Scene.GetPxScene()->raycast(U2PVector(Start), U2PVector(Dir), DeltaMag, HitBuffer, U2PHitFlags(OutputFlags), QueryFilterData, QueryCallback);
		SweepCapture.EndCapturePhysXRaycast(HitBuffer);

		FinalizeCapture(Serializer);
	}
	else
	{
		Scene.GetPxScene()->raycast(U2PVector(Start), U2PVector(Dir), DeltaMag, HitBuffer, U2PHitFlags(OutputFlags), QueryFilterData, QueryCallback);
	}
#endif
}

void LowLevelSweep(FPhysScene& Scene, const FPhysicsGeometry& QueryGeom, const FTransform& StartTM, const FVector& Dir, float DeltaMag, FPhysicsHitCallback<FHitSweep>& HitBuffer, EHitFlags OutputFlags, FQueryFlags QueryFlags, const FCollisionFilterData& Filter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase* QueryCallback, const FQueryDebugParams& DebugParams)
{
#if !defined(PHYSICS_INTERFACE_PHYSX) || !PHYSICS_INTERFACE_PHYSX
	if (const auto& SolverAccelerationStructure = Scene.GetScene().GetSpacialAcceleration())
	{
		FChaosSQAccelerator SQAccelerator(*SolverAccelerationStructure);
		{
			//ISQAccelerator* SQAccelerator = Scene.GetSQAccelerator();
			double Time = 0.0;
			{
				FScopedDurationTimer Timer(Time);
				SQAccelerator.Sweep(QueryGeom, StartTM, Dir, DeltaMag, HitBuffer, OutputFlags, QueryFilterData, *QueryCallback, DebugParams);
			}
			
			if (((SerializeSQs && Time * 1000.0 * 1000.0 > SerializeSQs) || (false)) && IsInGameThread())
			{
				FPhysTestSerializer Serializer;
				Serializer.SetPhysicsData(*Scene.GetSolver()->GetEvolution());
				FSQCapture& SweepCapture = Serializer.CaptureSQ();
				SweepCapture.StartCaptureChaosSweep(*Scene.GetSolver()->GetEvolution(), QueryGeom, StartTM, Dir, DeltaMag, OutputFlags, QueryFilterData, Filter, *QueryCallback);
				SweepCapture.EndCaptureChaosSweep(HitBuffer);

				FinalizeCapture(Serializer);
			}
		}

		/*if (SerializeSQs && IsInGameThread())
		{
			FPhysTestSerializer Serializer;
			Serializer.SetPhysicsData(*Scene.GetSolver()->GetEvolution());
			FSQCapture& SweepCapture = Serializer.CaptureSQ();
			SweepCapture.StartCaptureChaosSweep(*Scene.GetSolver()->GetEvolution(), QueryGeom, StartTM, Dir, DeltaMag, OutputFlags, QueryFilterData, Filter, *QueryCallback);
			SQAccelerator.Sweep(QueryGeom, StartTM, Dir, DeltaMag, HitBuffer, OutputFlags, QueryFilterData, *QueryCallback);
			SweepCapture.EndCaptureChaosSweep(HitBuffer);

			FinalizeCapture(Serializer);
		}
		else
		{
			//ISQAccelerator* SQAccelerator = Scene.GetSQAccelerator();
			double Time = 0.0;
			{
				FScopedDurationTimer Timer(Time);
				SQAccelerator.Sweep(QueryGeom, StartTM, Dir, DeltaMag, HitBuffer, OutputFlags, QueryFilterData, *QueryCallback);
			}
			if(Time > )
		}*/
	}
#else
	if (SerializeSQs | ReplaySQs)
	{
		FPhysTestSerializer Serializer;
		Serializer.SetPhysicsData(*Scene.GetPxScene());
		FSQCapture& SweepCapture = Serializer.CaptureSQ();
		SweepCapture.StartCapturePhysXSweep(*Scene.GetPxScene(), QueryGeom, StartTM, Dir, DeltaMag, OutputFlags, QueryFilterData, Filter, *QueryCallback);
		Scene.GetPxScene()->sweep(QueryGeom, U2PTransform(StartTM), U2PVector(Dir), DeltaMag, HitBuffer, U2PHitFlags(OutputFlags), QueryFilterData, QueryCallback);
		SweepCapture.EndCapturePhysXSweep(HitBuffer);

		FinalizeCapture(Serializer);
	}
	else
	{
		Scene.GetPxScene()->sweep(QueryGeom, U2PTransform(StartTM), U2PVector(Dir), DeltaMag, HitBuffer, U2PHitFlags(OutputFlags), QueryFilterData, QueryCallback);
	}
#endif
}

void LowLevelOverlap(FPhysScene& Scene, const FPhysicsGeometry& QueryGeom, const FTransform& GeomPose, FPhysicsHitCallback<FHitOverlap>& HitBuffer, FQueryFlags QueryFlags, const FCollisionFilterData& Filter, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase* QueryCallback, const FQueryDebugParams& DebugParams)
{
#if !defined(PHYSICS_INTERFACE_PHYSX) || !PHYSICS_INTERFACE_PHYSX
	if (const auto& SolverAccelerationStructure = Scene.GetScene().GetSpacialAcceleration())
	{
		FChaosSQAccelerator SQAccelerator(*SolverAccelerationStructure);
		if (false && SerializeSQs && IsInGameThread())
		{
			FPhysTestSerializer Serializer;
			Serializer.SetPhysicsData(*Scene.GetSolver()->GetEvolution());
			FSQCapture& OverlapCapture = Serializer.CaptureSQ();
			OverlapCapture.StartCaptureChaosOverlap(*Scene.GetSolver()->GetEvolution(), QueryGeom, GeomPose, QueryFilterData, Filter, *QueryCallback);
			SQAccelerator.Overlap(QueryGeom, GeomPose, HitBuffer, QueryFilterData, *QueryCallback);
			OverlapCapture.EndCaptureChaosOverlap(HitBuffer);

			FinalizeCapture(Serializer);
		}
		else
		{
			//ISQAccelerator* SQAccelerator = Scene.GetSQAccelerator();
			SQAccelerator.Overlap(QueryGeom, GeomPose, HitBuffer, QueryFilterData, *QueryCallback);
		}
	}
#else
	if (SerializeSQs | ReplaySQs)
	{
		FPhysTestSerializer Serializer;
		Serializer.SetPhysicsData(*Scene.GetPxScene());
		FSQCapture& SweepCapture = Serializer.CaptureSQ();
		SweepCapture.StartCapturePhysXOverlap(*Scene.GetPxScene(), QueryGeom, GeomPose, QueryFilterData, Filter, *QueryCallback);
		Scene.GetPxScene()->overlap(QueryGeom, U2PTransform(GeomPose), HitBuffer, QueryFilterData, QueryCallback);
		SweepCapture.EndCapturePhysXOverlap(HitBuffer);

		FinalizeCapture(Serializer);
	}
	else
	{
		Scene.GetPxScene()->overlap(QueryGeom, U2PTransform(GeomPose), HitBuffer, QueryFilterData, QueryCallback);
	}		
#endif
}

//#endif // WITH_PHYSX 