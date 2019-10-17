// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PhysXSupport.h: PhysX support
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "Misc/ScopeLock.h"
#include "EngineDefines.h"
#include "Containers/Queue.h"
#include "Physics/PhysicsFiltering.h"
#include "PhysXPublic.h"
#include "PhysXSupportCore.h"
#include "Serialization/BulkData.h"

class UBodySetup;
class UPhysicalMaterial;
struct FCollisionShape;
class FPhysScene_PhysX;

#if WITH_PHYSX

// binary serialization requires 128 byte alignment
#ifndef PHYSX_SERIALIZATION_ALIGNMENT
#define PHYSX_SERIALIZATION_ALIGNMENT 128
#endif

/** Thresholds for aggregates  */
const uint32 AggregateMaxSize	   = 128;
const uint32 AggregateBodyShapesThreshold	   = 999999999;


/////// UTILS




/** Calculates correct impulse at the body's center of mass and adds the impulse to the body. */
/** Util to see if a PxRigidBody is non-kinematic */
bool IsRigidBodyKinematic_AssumesLocked(const PxRigidBody* PRigidBody);

bool IsRigidBodyKinematicAndInSimulationScene_AssumesLocked(const PxRigidBody* PRigidBody);

/////// GLOBAL POINTERS
	
/** Total number of PhysX convex meshes around currently. */
extern int32					GNumPhysXConvexMeshes;

// The following are used for deferred cleanup - object that cannot be destroyed until all uses have been destroyed, but GC guarantees no order.

/** Array of PxConvexMesh objects which are awaiting cleaning up. */
extern TArray<PxConvexMesh*>	GPhysXPendingKillConvex;

/** Array of PxTriangleMesh objects which are awaiting cleaning up. */
extern ENGINE_API TArray<PxTriangleMesh*>	GPhysXPendingKillTriMesh;

/** Array of PxHeightField objects which are awaiting cleaning up. */
extern ENGINE_API TArray<PxHeightField*>	GPhysXPendingKillHeightfield;

/** Array of PxMaterial objects which are awaiting cleaning up. */
extern TArray<PxMaterial*>		GPhysXPendingKillMaterial;


extern const physx::PxQuat U2PSphylBasis;
extern const FQuat U2PSphylBasis_UE;

ENGINE_API PxCollection* MakePhysXCollection(const TArray<UPhysicalMaterial*>& PhysicalMaterials, const TArray<UBodySetup*>& BodySetups, uint64 BaseId);

/** Utility wrapper for a uint8 TArray for loading into PhysX. */
class FPhysXInputStream : public PxInputStream
{
public:
	/** Raw byte data */
	const uint8	*Data;
	/** Number of bytes */
	int32				DataSize;
	/** Current read position withing Data buffer */
	mutable uint32			ReadPos;

	FPhysXInputStream()
		: Data(NULL)
		, DataSize(0)
		, ReadPos(0)
	{}

	FPhysXInputStream(const uint8 *InData, const int32 InSize)
		: Data(InData)
		, DataSize( InSize )
		, ReadPos(0)
	{}

	virtual PxU32 read(void* Dest, PxU32 Count) override
	{
		check(Data);
		check(Dest);
		check(Count);
		const uint32 EndPos = ReadPos + Count;
		if( EndPos <= (uint32)DataSize )
		{
			FMemory::Memcpy( Dest, Data + ReadPos, Count );
			ReadPos = EndPos;
			return Count;
		}
		else
		{
			return 0;
		}
	}
};

/** Utility class for reading cooked physics data. */
class FPhysXCookingDataReader
{
public:
	TArray<PxConvexMesh*> ConvexMeshes;
	TArray<PxConvexMesh*> ConvexMeshesNegX;
	TArray<PxTriangleMesh*> TriMeshes;

	FPhysXCookingDataReader( FByteBulkData& InBulkData, struct FBodySetupUVInfo* UVInfo );

private:

	PxConvexMesh* ReadConvexMesh( FBufferReader& Ar, uint8* InBulkDataPtr, int32 InBulkDataSize );
	PxTriangleMesh* ReadTriMesh( FBufferReader& Ar, uint8* InBulkDataPtr, int32 InBulkDataSize );
};

/** 'Shader' used to filter simulation collisions. Could be called on any thread. */
PxFilterFlags PhysXSimFilterShader(	PxFilterObjectAttributes attributes0, PxFilterData filterData0, 
									PxFilterObjectAttributes attributes1, PxFilterData filterData1,
									PxPairFlags& pairFlags, const void* constantBlock, PxU32 constantBlockSize );

#if !WITH_CHAOS
/** Event callback used to notify engine about various collision events */
class ENGINE_API FPhysXSimEventCallback : public PxSimulationEventCallback
{
public:
	FPhysXSimEventCallback(FPhysScene* InOwningScene) : OwningScene(InOwningScene){}

	virtual void onConstraintBreak(PxConstraintInfo* constraints, PxU32 count) override;
	virtual void onWake(PxActor** actors, PxU32 count) override;
	virtual void onSleep(PxActor** actors, PxU32 count) override;
	virtual void onTrigger(PxTriggerPair* pairs, PxU32 count) override {}
	virtual void onContact(const PxContactPairHeader& PairHeader, const PxContactPair* Pairs, PxU32 NumPairs) override;
	virtual void onAdvance(const PxRigidBody*const* bodyBuffer, const PxTransform* poseBuffer, const PxU32 count) override {}

private:	
	FPhysScene* OwningScene;
};
#endif

class FPhysXProfilerCallback : public PxProfilerCallback
{

public:
	virtual void* zoneStart(const char* eventName, bool detached, uint64_t contextId) override;
	virtual void zoneEnd(void* profilerData, const char* eventName, bool detached, uint64_t contextId) override;

private:
};

struct FPhysXMbpBroadphaseCallback : public PxBroadPhaseCallback
{

public:
	virtual void onObjectOutOfBounds(PxShape& InShape, PxActor& InActor) override;
	virtual void onObjectOutOfBounds(PxAggregate& InAggregate) override;

};

#endif // WITH_PHYSX
