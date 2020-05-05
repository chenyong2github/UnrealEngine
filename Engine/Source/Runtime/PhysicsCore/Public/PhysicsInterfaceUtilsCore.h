// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PhysicsInterfaceTypesCore.h"
#include "PhysicsInterfaceDeclaresCore.h"

#if PHYSICS_INTERFACE_PHYSX
#include "PhysXPublicCore.h"
#include "Containers/Union.h"
#include "CollisionShape.h"
#endif

PHYSICSCORE_API FCollisionFilterData C2UFilterData(const FChaosFilterData& FilterData);
PHYSICSCORE_API FChaosFilterData U2CFilterData(const FCollisionFilterData& FilterData);

#if PHYSICS_INTERFACE_PHYSX
PHYSICSCORE_API FCollisionFilterData ToUnrealFilterData(const PxFilterData& FilterData);
#else
PHYSICSCORE_API FCollisionFilterData ToUnrealFilterData(const FChaosFilterData& FilterData);
#endif

#if PHYSICS_INTERFACE_PHYSX
PHYSICSCORE_API FCollisionFilterData P2UFilterData(const PxFilterData& PFilterData);
PHYSICSCORE_API PxFilterData U2PFilterData(const FCollisionFilterData& FilterData);
PHYSICSCORE_API PxShapeFlags BuildPhysXShapeFlags(FBodyCollisionFlags BodyCollisionFlags, bool bPhysicsStatic, bool bIsTriangleMesh);
PHYSICSCORE_API PxGeometryType::Enum U2PCollisionShapeType(ECollisionShapeType InUType);
PHYSICSCORE_API ECollisionShapeType P2UCollisionShapeType(PxGeometryType::Enum InPType);

template<typename AGGREGATE_FLAG_TYPE, typename FLAG_TYPE>
inline void ModifyFlag_Default(AGGREGATE_FLAG_TYPE& Flags, const FLAG_TYPE FlagToSet, const bool bValue)
{
	if (bValue)
	{
		Flags |= FlagToSet;
	}
	else
	{
		Flags.clear(FlagToSet);
	}
}

template<const PxActorFlag::Enum FlagToSet>
inline void ModifyActorFlag(physx::PxActorFlags& Flags, const bool bValue)
{
	ModifyFlag_Default(Flags, FlagToSet, bValue);
}

template<const PxShapeFlag::Enum FlagToSet>
inline void ModifyShapeFlag(physx::PxShapeFlags& Flags, const bool bValue)
{
	ModifyFlag_Default(Flags, FlagToSet, bValue);
}

template<const PxRigidBodyFlag::Enum FlagToSet>
inline void ModifyRigidBodyFlag(physx::PxRigidBodyFlags& Flags, const bool bValue)
{
	ModifyFlag_Default(Flags, FlagToSet, bValue);
}

template<>
inline void ModifyRigidBodyFlag<PxRigidBodyFlag::eKINEMATIC>(physx::PxRigidBodyFlags& Flags, const bool bValue)
{
	// Objects can't be CCD and Kinematic at the same time.
	// If enabling Kinematic and CCD is on, disable it and turn on Speculative CCD instead.
	if (bValue && Flags.isSet(PxRigidBodyFlag::eENABLE_CCD))
	{
		Flags |= PxRigidBodyFlag::eKINEMATIC;
		Flags |= PxRigidBodyFlag::eENABLE_SPECULATIVE_CCD;
		Flags.clear(PxRigidBodyFlag::eENABLE_CCD);
	}

	// If disabling Kinematic and Speculative CCD is on, disable it and turn on CCD instead.
	else if (!bValue && Flags.isSet(PxRigidBodyFlag::eENABLE_SPECULATIVE_CCD))
	{
		Flags |= PxRigidBodyFlag::eENABLE_CCD;
		Flags.clear(PxRigidBodyFlag::eENABLE_SPECULATIVE_CCD);
		Flags.clear(PxRigidBodyFlag::eKINEMATIC);
	}

	// No sanitization is needed.
	else
	{
		ModifyFlag_Default(Flags, PxRigidBodyFlag::eKINEMATIC, bValue);
	}
}

template<>
inline void ModifyRigidBodyFlag<PxRigidBodyFlag::eENABLE_CCD>(physx::PxRigidBodyFlags& Flags, const bool bValue)
{
	// Objects can't be CCD and Kinematic at the same time.
	// If disabling CCD and Speculative CCD is on, disable it too.
	if (!bValue && Flags.isSet(PxRigidBodyFlag::eENABLE_SPECULATIVE_CCD))
	{
		// CCD shouldn't be enabled at this point, but force disable just in case.
		Flags.clear(PxRigidBodyFlag::eENABLE_CCD);
		Flags.clear(PxRigidBodyFlag::eENABLE_SPECULATIVE_CCD);
	}

	// If enabling CCD but Kinematic is on, enable Speculative CCD instead.
	else if (bValue && Flags.isSet(PxRigidBodyFlag::eKINEMATIC))
	{
		Flags |= PxRigidBodyFlag::eENABLE_SPECULATIVE_CCD;
	}

	// No sanitization is needed.
	else
	{
		ModifyFlag_Default(Flags, PxRigidBodyFlag::eENABLE_CCD, bValue);
	}
}

template<const PxActorFlag::Enum FlagToSet>
inline void ModifyActorFlag_Isolated(PxActor* PActor, const bool bValue)
{
	PxActorFlags ActorFlags = PActor->getActorFlags();
	ModifyActorFlag<FlagToSet>(ActorFlags, bValue);
	PActor->setActorFlags(ActorFlags);
}

template<const PxRigidBodyFlag::Enum FlagToSet>
inline void ModifyRigidBodyFlag_Isolated(PxRigidBody* PRigidBody, const bool bValue)
{
	PxRigidBodyFlags RigidBodyFlags = PRigidBody->getRigidBodyFlags();
	ModifyRigidBodyFlag<FlagToSet>(RigidBodyFlags, bValue);
	PRigidBody->setRigidBodyFlags(RigidBodyFlags);
}

template<const PxShapeFlag::Enum FlagToSet>
inline void ModifyShapeFlag_Isolated(PxShape* PShape, const bool bValue)
{
	PxShapeFlags ShapeFlags = PShape->getFlags();
	ModifyShapeFlag<FlagToSet>(ShapeFlags, bValue);
	PShape->setFlags(ShapeFlags);
}

// MISC

/** Convert from unreal to physx capsule rotation */
PHYSICSCORE_API FQuat ConvertToPhysXCapsuleRot(const FQuat& GeomRot);
/** Convert from physx to unreal capsule rotation */
PHYSICSCORE_API FQuat ConvertToUECapsuleRot(const PxQuat & GeomRot);
PHYSICSCORE_API FQuat ConvertToUECapsuleRot(const FQuat & GeomRot);
/** Convert from unreal to physx capsule pose */
PHYSICSCORE_API PxTransform ConvertToPhysXCapsulePose(const FTransform& GeomPose);

// FILTER DATA

// Adapts a FCollisionShape to a PxGeometry type, used for various queries
struct PHYSICSCORE_API FPhysXShapeAdapter
{
public:
	FPhysXShapeAdapter(const FQuat& Rot, const FCollisionShape& CollisionShape);

	const PxGeometry& GetGeometry() const
	{
		return GeometryHolder.any();
	}

public:
	FTransform GetGeomPose(const FVector& Pos) const
	{
		return FTransform(Rotation, Pos);
	}

	const FQuat& GetGeomOrientation() const
	{
		return Rotation;
	}

private:
	PxGeometryHolder GeometryHolder;
	FQuat Rotation;
};

#endif // WITH_PHYX

//Find the face index for a given hit. This gives us a chance to modify face index based on things like most opposing normal
PHYSICSCORE_API uint32 FindFaceIndex(const FHitLocation& PHit, const FVector& UnitDirection);
