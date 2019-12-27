// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PhysXSupport.h: PhysX support
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"

#if WITH_PHYSX
#include "PhysXIncludes.h"
#include "PhysicsInterfaceTypesCore.h"
#include "CollisionShape.h"

// Whether or not to use the PhysX scene lock
#ifndef USE_SCENE_LOCK
#define USE_SCENE_LOCK			1
#endif

#if USE_SCENE_LOCK

DECLARE_CYCLE_STAT_EXTERN(TEXT("PhysX Scene ReadLock"), STAT_PhysSceneReadLock, STATGROUP_Physics, PHYSICSCORE_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("PhysX Scene WriteLock"), STAT_PhysSceneWriteLock, STATGROUP_Physics, PHYSICSCORE_API);

/** Scoped scene read lock - we use this instead of PxSceneReadLock because it handles NULL scene */
class FPhysXSceneReadLock
{
public:
	
	FPhysXSceneReadLock(PxScene* PInScene, const char* filename, PxU32 lineno)
		: PScene(PInScene)
	{
		SCOPE_CYCLE_COUNTER(STAT_PhysSceneReadLock);
		if(PScene)
		{
			PScene->lockRead(filename, lineno);
		}
	}

	~FPhysXSceneReadLock()
	{
		if(PScene)
		{
			PScene->unlockRead();
		}
	}

private:
	PxScene* PScene;
};

#if WITH_APEX
/** Scoped scene read lock - we use this instead of PxSceneReadLock because it handles NULL scene */
class FApexSceneReadLock
{
public:

	FApexSceneReadLock(nvidia::apex::Scene* PInScene, const char* filename, PxU32 lineno)
		: PScene(PInScene)
	{
		SCOPE_CYCLE_COUNTER(STAT_PhysSceneReadLock);
		if (PScene)
		{
			PScene->lockRead(filename, lineno);
		}
	}

	~FApexSceneReadLock()
	{
		if (PScene)
		{
			PScene->unlockRead();
		}
	}

private:
	nvidia::apex::Scene* PScene;
};
#endif

/** Scoped scene write lock - we use this instead of PxSceneReadLock because it handles NULL scene */
class FPhysXSceneWriteLock
{
public:
	FPhysXSceneWriteLock(PxScene* PInScene, const char* filename, PxU32 lineno)
		: PScene(PInScene)
	{
		SCOPE_CYCLE_COUNTER(STAT_PhysSceneWriteLock);
		if(PScene)
		{
			PScene->lockWrite(filename, lineno);
		}
	}

	~FPhysXSceneWriteLock()
	{
		if(PScene)
		{
			PScene->unlockWrite();
		}
	}

private:
	PxScene* PScene;
};

#if WITH_APEX
/** Scoped scene write lock - we use this instead of PxSceneReadLock because it handles NULL scene */
class FApexSceneWriteLock
{
public:
	FApexSceneWriteLock(nvidia::apex::Scene* PInScene, const char* filename, PxU32 lineno)
		: PScene(PInScene)
	{
		SCOPE_CYCLE_COUNTER(STAT_PhysSceneWriteLock);
		if (PScene)
		{
			PScene->lockWrite(filename, lineno);
		}
	}

	~FApexSceneWriteLock()
	{
		if (PScene)
		{
			PScene->unlockWrite();
		}
	}

private:
	nvidia::apex::Scene* PScene;
};
#endif

#define SCOPED_SCENE_READ_LOCK( _scene ) FPhysXSceneReadLock PREPROCESSOR_JOIN(_rlock,__LINE__)(_scene, __FILE__, __LINE__)
#define SCOPED_SCENE_WRITE_LOCK( _scene ) FPhysXSceneWriteLock PREPROCESSOR_JOIN(_wlock,__LINE__)(_scene, __FILE__, __LINE__)
#if WITH_APEX
#define SCOPED_APEX_SCENE_READ_LOCK( _scene ) FApexSceneReadLock PREPROCESSOR_JOIN(_rlock,__LINE__)(_scene, __FILE__, __LINE__)
#define SCOPED_APEX_SCENE_WRITE_LOCK( _scene ) FApexSceneWriteLock PREPROCESSOR_JOIN(_wlock,__LINE__)(_scene, __FILE__, __LINE__)
#endif

#define SCENE_LOCK_READ( _scene )		{ SCOPE_CYCLE_COUNTER(STAT_PhysSceneReadLock); if((_scene) != NULL) { (_scene)->lockRead(__FILE__, __LINE__); } }
#define SCENE_UNLOCK_READ( _scene )		{ if((_scene) != NULL) { (_scene)->unlockRead(); } }
#define SCENE_LOCK_WRITE( _scene )		{ SCOPE_CYCLE_COUNTER(STAT_PhysSceneWriteLock); if((_scene) != NULL) { (_scene)->lockWrite(__FILE__, __LINE__); } }
#define SCENE_UNLOCK_WRITE( _scene )	{ if((_scene) != NULL) { (_scene)->unlockWrite(); } }
#else
#define SCOPED_SCENE_READ_LOCK_INDEXED( _scene, _index )
#define SCOPED_SCENE_READ_LOCK( _scene )
#define SCOPED_SCENE_WRITE_LOCK_INDEXED( _scene, _index )
#define SCOPED_SCENE_WRITE_LOCK( _scene )
#define SCENE_LOCK_READ( _scene )
#define SCENE_UNLOCK_READ( _scene )
#define SCENE_LOCK_WRITE( _scene )
#define SCENE_UNLOCK_WRITE( _scene )

#if WITH_APEX
#define SCOPED_APEX_SCENE_READ_LOCK( _scene )
#define SCOPED_APEX_SCENE_WRITE_LOCK( _scene )
#endif
#endif

//////// BASIC TYPE CONVERSION

/** Convert Unreal FMatrix to PhysX PxTransform */
PHYSICSCORE_API PxTransform UMatrix2PTransform(const FMatrix& UTM);
/** Convert Unreal FMatrix to PhysX PxMat44 */
PHYSICSCORE_API PxMat44 U2PMatrix(const FMatrix& UTM);
/** Convert PhysX PxTransform to Unreal PxTransform */
PHYSICSCORE_API FTransform P2UTransform(const PxTransform& PTM);
/** Convert PhysX PxMat44 to Unreal FMatrix */
PHYSICSCORE_API FMatrix P2UMatrix(const PxMat44& PMat);
/** Convert PhysX PxTransform to Unreal FMatrix */
PHYSICSCORE_API FMatrix PTransform2UMatrix(const PxTransform& PTM);

// inlines

/** Convert Unreal FVector to PhysX PxVec3 */
FORCEINLINE_DEBUGGABLE PxVec3 U2PVector(const FVector& UVec)
{
	return PxVec3(UVec.X, UVec.Y, UVec.Z);
}

FORCEINLINE_DEBUGGABLE PxVec4 U2PVector(const FVector4& UVec)
{
	return PxVec4(UVec.X, UVec.Y, UVec.Z, UVec.W);
}

/** Convert Unreal FQuat to PhysX PxQuat */
FORCEINLINE_DEBUGGABLE PxQuat U2PQuat(const FQuat& UQuat)
{
	return PxQuat( UQuat.X, UQuat.Y, UQuat.Z, UQuat.W );
}

/** Convert Unreal FPlane to PhysX plane def */
FORCEINLINE_DEBUGGABLE PxPlane U2PPlane(const FPlane& Plane)
{
	return PxPlane(Plane.X, Plane.Y, Plane.Z, -Plane.W);
}

/** Convert PhysX PxVec3 to Unreal FVector */
FORCEINLINE_DEBUGGABLE FVector P2UVector(const PxVec3& PVec)
{
	return FVector(PVec.x, PVec.y, PVec.z);
}

FORCEINLINE_DEBUGGABLE FVector4 P2UVector(const PxVec4& PVec)
{
	return FVector4(PVec.x, PVec.y, PVec.z, PVec.w);
}

/** Convert PhysX PxQuat to Unreal FQuat */
FORCEINLINE_DEBUGGABLE FQuat P2UQuat(const PxQuat& PQuat)
{
	return FQuat(PQuat.x, PQuat.y, PQuat.z, PQuat.w);
}

/** Convert PhysX plane def to Unreal FPlane */
FORCEINLINE_DEBUGGABLE FPlane P2UPlane(const PxReal P[4])
{
	return FPlane(P[0], P[1], P[2], -P[3]);
}

FORCEINLINE_DEBUGGABLE FPlane P2UPlane(const PxPlane& Plane)
{
	return FPlane(Plane.n.x, Plane.n.y, Plane.n.z, -Plane.d);
}

/** Convert PhysX Barycentric Vec3 to FVector4 */
FORCEINLINE_DEBUGGABLE FVector4 P2U4BaryCoord(const PxVec3& PVec)
{
	return FVector4(PVec.x, PVec.y, 1.f - PVec.x - PVec.y, PVec.z);
}

/** Convert PhysX Geometry Type to ECollisionShapeType */
inline ECollisionShapeType P2UGeometryType(PxGeometryType::Enum Type)
{
	switch(Type)
	{
	case PxGeometryType::eSPHERE: return ECollisionShapeType::Sphere;
	case PxGeometryType::ePLANE: return ECollisionShapeType::Plane;
	case PxGeometryType::eCAPSULE: return ECollisionShapeType::Capsule;
	case PxGeometryType::eBOX: return ECollisionShapeType::Box;
	case PxGeometryType::eCONVEXMESH: return ECollisionShapeType::Convex;
	case PxGeometryType::eTRIANGLEMESH: return ECollisionShapeType::Trimesh;
	case PxGeometryType::eHEIGHTFIELD: return ECollisionShapeType::Heightfield;
	default: return ECollisionShapeType::None;
	}
}

/** Convert ECollisionShapeType to PhysX Geometry Type */
inline PxGeometryType::Enum U2PGeometryType(ECollisionShapeType Type)
{
	switch(Type)
	{
	case ECollisionShapeType::Sphere: return PxGeometryType::eSPHERE;
	case ECollisionShapeType::Plane: return PxGeometryType::ePLANE;
	case ECollisionShapeType::Capsule: return PxGeometryType::eCAPSULE;
	case ECollisionShapeType::Box: return PxGeometryType::eBOX;
	case ECollisionShapeType::Convex: return PxGeometryType::eCONVEXMESH;
	case ECollisionShapeType::Trimesh: return PxGeometryType::eTRIANGLEMESH;
	case ECollisionShapeType::Heightfield: return PxGeometryType::eHEIGHTFIELD;
	default: return PxGeometryType::eINVALID;
	}
}

/** Convert Unreal FTransform to PhysX PxTransform */
FORCEINLINE_DEBUGGABLE PxTransform U2PTransform(const FTransform& UTransform)
{
	PxQuat PQuat = U2PQuat(UTransform.GetRotation());
	PxVec3 PPos = U2PVector(UTransform.GetLocation());

	PxTransform Result(PPos, PQuat);

	return Result;
}

namespace nvidia
{
	namespace apex
	{
		class  PhysX3Interface;
	}
}

/** The default interface is nullptr. This can be set by other modules to get custom behavior */
extern PHYSICSCORE_API nvidia::apex::PhysX3Interface* GPhysX3Interface;

struct PHYSICSCORE_API FContactModifyCallback : public PxContactModifyCallback
{
	virtual ~FContactModifyCallback() {}	//This should only be called from the factory's destroy method which is called after simulation is done.
};

struct PHYSICSCORE_API FCCDContactModifyCallback : public PxCCDContactModifyCallback
{
	virtual ~FCCDContactModifyCallback() {}	//This should only be called from the factory's destroy method which is called after simulation is done.
};

//////// GEOM CONVERSION
// we need this helper struct since PhysX needs geoms to be on the stack
struct UCollision2PGeom
{
	UCollision2PGeom(const FCollisionShape& CollisionShape);
	const PxGeometry * GetGeometry() { return (PxGeometry*)Storage; }
private:

	union StorageUnion
	{
		char box[sizeof(PxBoxGeometry)];
		char sphere[sizeof(PxSphereGeometry)];
		char capsule[sizeof(PxCapsuleGeometry)];
	};	//we need this because we can't use real union since these structs have non trivial constructors

	char Storage[sizeof(StorageUnion)];
};

#endif