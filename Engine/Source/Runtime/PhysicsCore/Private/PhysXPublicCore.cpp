// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_PHYSX

#include "PhysXPublicCore.h"
#include "PhysicsPublicCore.h"
#include "IPhysXCookingModule.h"
#include "Modules/ModuleManager.h"

#if WITH_APEX
PHYSICSCORE_API nvidia::apex::PhysX3Interface* GPhysX3Interface = nullptr;
#endif	// #if WITH_APEX

///////////////////// Unreal to PhysX conversion /////////////////////

PxTransform UMatrix2PTransform(const FMatrix& UTM)
{
	PxQuat PQuat = U2PQuat(UTM.ToQuat());
	PxVec3 PPos = U2PVector(UTM.GetOrigin());

	PxTransform Result(PPos, PQuat);

	return Result;
}

PxMat44 U2PMatrix(const FMatrix& UTM)
{
	PxMat44 Result;

	const physx::PxMat44 *MatPtr = (const physx::PxMat44 *)(&UTM);
	Result = *MatPtr;

	return Result;
}

UCollision2PGeom::UCollision2PGeom(const FCollisionShape& CollisionShape)
{
	switch (CollisionShape.ShapeType)
	{
	case ECollisionShape::Box:
	{
		new (Storage)PxBoxGeometry(U2PVector(CollisionShape.GetBox()));
		break;
	}
	case ECollisionShape::Sphere:
	{
		new (Storage)PxSphereGeometry(CollisionShape.GetSphereRadius());
		break;
	}
	case ECollisionShape::Capsule:
	{
		new (Storage)PxCapsuleGeometry(CollisionShape.GetCapsuleRadius(), CollisionShape.GetCapsuleAxisHalfLength());
		break;
	}
	default:
		// invalid point
		ensure(false);
	}
}

///////////////////// PhysX to Unreal conversion /////////////////////

FMatrix P2UMatrix(const PxMat44& PMat)
{
	FMatrix Result;
	// we have to use Memcpy instead of typecasting, because PxMat44's are not aligned like FMatrix is
	FMemory::Memcpy(&Result, &PMat, sizeof(PMat));
	return Result;
}

FMatrix PTransform2UMatrix(const PxTransform& PTM)
{
	FQuat UQuat = P2UQuat(PTM.q);
	FVector UPos = P2UVector(PTM.p);

	FMatrix Result = FQuatRotationTranslationMatrix(UQuat, UPos);

	return Result;
}

FTransform P2UTransform(const PxTransform& PTM)
{
	FQuat UQuat = P2UQuat(PTM.q);
	FVector UPos = P2UVector(PTM.p);

	FTransform Result = FTransform(UQuat, UPos);

	return Result;
}

IPhysXCookingModule* GetPhysXCookingModule(bool bForceLoad)
{
	check(IsInGameThread());

	if (bForceLoad)
	{
#if WITH_PHYSX_COOKING
		return FModuleManager::LoadModulePtr<IPhysXCookingModule>("PhysXCooking");	//in some configurations (for example the editor) we must have physx cooking
#else
		return FModuleManager::LoadModulePtr<IPhysXCookingModule>("RuntimePhysXCooking");	//in some configurations (mobile) we can choose to opt in for physx cooking via plugin
#endif
	}
	else
	{
#if WITH_PHYSX_COOKING
		return FModuleManager::GetModulePtr<IPhysXCookingModule>("PhysXCooking");	//in some configurations (for example the editor) we must have physx cooking
#else
		return FModuleManager::GetModulePtr<IPhysXCookingModule>("RuntimePhysXCooking");	//in some configurations (mobile) we can choose to opt in for physx cooking via plugin
#endif
	}
}
#endif // WITH_PHYSX
