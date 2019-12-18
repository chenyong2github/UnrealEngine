// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#ifndef PHYSICS_INTERFACE_PHYSX
#define PHYSICS_INTERFACE_PHYSX 0
#endif

#ifndef WITH_CHAOS
#define WITH_CHAOS 0
#endif

#ifndef WITH_IMMEDIATE_PHYSX
#define WITH_IMMEDIATE_PHYSX 0
#endif

#if WITH_CHAOS

#include "ChaosSQTypes.h"

namespace Chaos
{
	class FImplicitObject;

	template<class T>
	class TCapsule;

	template <typename T, int d>
	class TGeometryParticle;

	template <typename T, int d>
	class TPerShapeData;

	class FPhysicalMaterial;

	template<typename, uint32, uint32>
	class THandle;

	struct FMaterialHandle;

	class FChaosPhysicsMaterial;
}

// Temporary dummy types until SQ implemented
namespace ChaosInterface
{
	struct FDummyPhysType;
	struct FDummyPhysActor;
	template<typename T> struct FDummyCallback;
}
using FPhysTypeDummy = ChaosInterface::FDummyPhysType;
using FPhysActorDummy = ChaosInterface::FDummyPhysActor;

template<typename T>
using FCallbackDummy = ChaosInterface::FDummyCallback<T>;

struct FTransform;

using FHitLocation = ChaosInterface::FLocationHit;
using FHitSweep = ChaosInterface::FSweepHit;
using FHitRaycast = ChaosInterface::FRaycastHit;
using FHitOverlap = ChaosInterface::FOverlapHit;
using FPhysicsQueryHit = ChaosInterface::FQueryHit;

using FPhysicsTransform = FTransform;

using FPhysicsShape = Chaos::TPerShapeData<float, 3>;
using FPhysicsGeometry = Chaos::FImplicitObject;
using FPhysicsCapsuleGeometry = Chaos::TCapsule<float>;
using FPhysicsMaterial = Chaos::FChaosPhysicsMaterial;
using FPhysicsActor = Chaos::TGeometryParticle<float,3>;

template <typename T>
using FPhysicsHitCallback = ChaosInterface::FSQHitBuffer<T>;

template <typename T>
using FSingleHitBuffer = ChaosInterface::FSQSingleHitBuffer<T>;

template <typename T>
using FDynamicHitBuffer = ChaosInterface::FSQHitBuffer<T>;

#elif PHYSICS_INTERFACE_PHYSX

namespace physx
{
	struct PxLocationHit;
	struct PxSweepHit;
	struct PxRaycastHit;
	struct PxOverlapHit;
	struct PxQueryHit;

	class PxTransform;
	class PxShape;
	class PxGeometry;
	class PxCapsuleGeometry;
	class PxMaterial;
	class PxRigidActor;

	template<typename T>
	struct PxHitBuffer;
	
	template<typename T>
	struct PxHitCallback;
}

namespace PhysXInterface
{
	struct FDummyPhysType {};
}

using FHitLocation = physx::PxLocationHit;
using FHitSweep = physx::PxSweepHit;
using FHitRaycast = physx::PxRaycastHit;
using FHitOverlap = physx::PxOverlapHit;
using FPhysicsQueryHit = physx::PxQueryHit;

using FPhysicsTransform = physx::PxTransform;
using FPhysTypeDummy = PhysXInterface::FDummyPhysType;

using FPhysicsShape = physx::PxShape;
using FPhysicsGeometry = physx::PxGeometry;
using FPhysicsCapsuleGeometry = physx::PxCapsuleGeometry;
using FPhysicsMaterial = physx::PxMaterial;
using FPhysicsActor = physx::PxRigidActor;

template <typename T>
using FPhysicsHitCallback = physx::PxHitCallback<T>;

struct FQueryDebugParams {};

#else

static_assert(false, "A physics engine interface must be defined to build");

#endif

#if PHYSICS_INTERFACE_PHYSX

#if WITH_IMMEDIATE_PHYSX

struct FPhysicsActorReference_ImmediatePhysX;
struct FPhysicsConstraintReference_ImmediatePhysX;
struct FPhysicsInterface_ImmediatePhysX;
class FPhysScene_ImmediatePhysX;
struct FPhysicsAggregateReference_ImmediatePhysX;
struct FPhysicsCommand_ImmediatePhysX;
struct FPhysicsShapeReference_ImmediatePhysX;
struct FPhysicsMaterialReference_ImmediatePhysX;
struct FPhysicsGeometryCollection_ImmediatePhysX;
struct FPhysXShapeAdapter;
struct FPhysxUserData;

typedef FPhysicsActorReference_ImmediatePhysX		FPhysicsActorHandle;
typedef FPhysicsConstraintReference_ImmediatePhysX	FPhysicsConstraintHandle;
typedef FPhysicsInterface_ImmediatePhysX			FPhysicsInterface;
typedef FPhysScene_ImmediatePhysX					FPhysScene;
typedef FPhysicsAggregateReference_ImmediatePhysX	FPhysicsAggregateHandle;
typedef FPhysicsCommand_ImmediatePhysX				FPhysicsCommand;
typedef FPhysicsShapeReference_ImmediatePhysX		FPhysicsShapeHandle;
typedef FPhysicsGeometryCollection_ImmediatePhysX	FPhysicsGeometryCollection;
typedef FPhysicsMaterialReference_ImmediatePhysX	FPhysicsMaterialHandle;
typedef FPhysXShapeAdapter							FPhysicsShapeAdapter;
typedef FPhysxUserData								FPhysicsUserData;

inline FPhysicsActorHandle DefaultPhysicsActorHandle() { return FPhysicsActorHandle(); }

#else

struct FPhysicsActorHandle_PhysX;
struct FPhysicsConstraintHandle_PhysX;
struct FPhysicsInterface_PhysX;
class FPhysScene_PhysX;
struct FPhysicsAggregateHandle_PhysX;
struct FPhysicsCommand_PhysX;
struct FPhysicsShapeHandle_PhysX;
struct FPhysicsGeometryCollection_PhysX;
struct FPhysicsMaterialHandle_PhysX;
struct FPhysXShapeAdapter;
struct FPhysxUserData;

typedef FPhysicsActorHandle_PhysX			FPhysicsActorHandle;
typedef FPhysicsConstraintHandle_PhysX		FPhysicsConstraintHandle;
typedef FPhysicsInterface_PhysX				FPhysicsInterface;
typedef FPhysScene_PhysX					FPhysScene;
typedef FPhysicsAggregateHandle_PhysX		FPhysicsAggregateHandle;
typedef FPhysicsCommand_PhysX				FPhysicsCommand;
typedef FPhysicsShapeHandle_PhysX			FPhysicsShapeHandle;
typedef FPhysicsGeometryCollection_PhysX	FPhysicsGeometryCollection;
typedef FPhysicsMaterialHandle_PhysX		FPhysicsMaterialHandle;
typedef FPhysXShapeAdapter					FPhysicsShapeAdapter;
typedef FPhysxUserData						FPhysicsUserData;

extern FPhysicsActorHandle DefaultPhysicsActorHandle();

#endif

#elif WITH_CHAOS

using FPhysicsActorHandle = Chaos::TGeometryParticle<float, 3>*;

class FChaosSceneId;
class FPhysInterface_Chaos;
class FPhysicsConstraintReference_Chaos;
class FPhysicsAggregateReference_Chaos;
class FPhysicsShapeReference_Chaos;
class FPhysScene_ChaosInterface;
class FPhysicsShapeAdapter_Chaos;
struct FPhysicsGeometryCollection_Chaos;
class FPhysicsUserData_Chaos;

typedef FPhysicsConstraintReference_Chaos	FPhysicsConstraintHandle;
typedef FPhysInterface_Chaos				FPhysicsInterface;
typedef FPhysScene_ChaosInterface			FPhysScene;
typedef FPhysicsAggregateReference_Chaos	FPhysicsAggregateHandle;
typedef FPhysInterface_Chaos				FPhysicsCommand;
typedef FPhysicsShapeReference_Chaos		FPhysicsShapeHandle;
typedef FPhysicsGeometryCollection_Chaos	FPhysicsGeometryCollection;
typedef Chaos::FMaterialHandle				FPhysicsMaterialHandle;
typedef FPhysicsShapeAdapter_Chaos			FPhysicsShapeAdapter;
typedef FPhysicsUserData_Chaos				FPhysicsUserData;

inline FPhysicsActorHandle DefaultPhysicsActorHandle() { return nullptr; }

#else

static_assert(false, "A physics engine interface must be defined to build");

#endif
