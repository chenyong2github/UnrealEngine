// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "Box.h"
#include "Chaos/Framework/PhysicsProxyBase.h"
#include "Chaos/PBDConstraintContainer.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/Vector.h"


class UPhysicalMaterial;

namespace Chaos
{
	/**
	 * Collision event data stored for use by other systems (e.g. Niagara, gameplay events)
	 */
	struct FCollidingData
	{
		FCollidingData()
			: Location(FVec3((FReal)0.0))
			, AccumulatedImpulse(FVec3((FReal)0.0))
			, Normal(FVec3((FReal)0.0))
			, Velocity1(FVec3((FReal)0.0))
			, Velocity2(FVec3((FReal)0.0))
		    , DeltaVelocity1(FVec3((FReal)0.0))
		    , DeltaVelocity2(FVec3((FReal)0.0))
			, AngularVelocity1(FVec3((FReal)0.0))
			, AngularVelocity2(FVec3((FReal)0.0))
			, Mass1((FReal)0.0)
			, Mass2((FReal)0.0)
			, PenetrationDepth((FReal)0.0)
			, Particle(nullptr)
			, Levelset(nullptr)
			, ParticleProxy(nullptr)
		    , LevelsetProxy(nullptr)
		{}

		FCollidingData(FVec3 InLocation, FVec3 InAccumulatedImpulse, FVec3 InNormal, FVec3 InVelocity1, FVec3 InVelocity2, FVec3 InDeltaVelocity1, FVec3 InDeltaVelocity2
		, FVec3 InAngularVelocity1, FVec3 InAngularVelocity2, FReal InMass1, FReal InMass2, FReal InPenetrationDepth, FGeometryParticle* InParticle
		, FGeometryParticle* InLevelset, IPhysicsProxyBase* InParticleProxy, IPhysicsProxyBase* InLevelsetProxy)
		    : Location(InLocation)
			, AccumulatedImpulse(InAccumulatedImpulse)
			, Normal(InNormal)
			, Velocity1(InVelocity1)
			, Velocity2(InVelocity2)
			, DeltaVelocity1(InDeltaVelocity1)
			, DeltaVelocity2(InDeltaVelocity2)
			, AngularVelocity1(InAngularVelocity1)
			, AngularVelocity2(InAngularVelocity2)
			, Mass1(InMass1)
			, Mass2(InMass2)
			, PenetrationDepth(InPenetrationDepth)
			, Particle(InParticle)
			, Levelset(InLevelset)
			, ParticleProxy(InParticleProxy)
		    , LevelsetProxy(InLevelsetProxy)
		{}

		bool IsValid() { return (ParticleProxy && LevelsetProxy); }

		FVec3 Location;
		FVec3 AccumulatedImpulse;
		FVec3 Normal;
		FVec3 Velocity1, Velocity2;
		FVec3 DeltaVelocity1, DeltaVelocity2;
		FVec3 AngularVelocity1, AngularVelocity2;
		FReal Mass1, Mass2;
		FReal PenetrationDepth;
		FGeometryParticle* Particle;
		FGeometryParticle* Levelset;
		IPhysicsProxyBase* ParticleProxy;
		IPhysicsProxyBase* LevelsetProxy;
	};

	/*
	CollisionData used in the subsystems
	*/
	struct FCollidingDataExt
	{
		FCollidingDataExt()
			: Location(FVec3((FReal)0.0))
			, AccumulatedImpulse(FVec3((FReal)0.0))
			, Normal(FVec3((FReal)0.0))
			, Velocity1(FVec3((FReal)0.0))
			, Velocity2(FVec3((FReal)0.0))
			, AngularVelocity1(FVec3((FReal)0.0))
			, AngularVelocity2(FVec3((FReal)0.0))
			, Mass1((FReal)0.0)
			, Mass2((FReal)0.0)
			, Particle(nullptr)
			, Levelset(nullptr)
		    , ParticleProxy(nullptr)
		    , LevelsetProxy(nullptr)
			, BoundingboxVolume((FReal)-1.0)
			, BoundingboxExtentMin((FReal)-1.0)
			, BoundingboxExtentMax((FReal)-1.0)
			, SurfaceType(-1)
		{}

		FCollidingDataExt(
		    FVec3 InLocation, FVec3 InAccumulatedImpulse, FVec3 InNormal, FVec3 InVelocity1, FVec3 InVelocity2
			, FVec3 InAngularVelocity1, FVec3 InAngularVelocity2, FReal InMass1, FReal InMass2, FGeometryParticle* InParticle
			, FGeometryParticle* InLevelset, IPhysicsProxyBase* InParticleProxy, IPhysicsProxyBase* InLevelsetProxy
			, FReal InBoundingboxVolume, FReal InBoundingboxExtentMin, FReal InBoundingboxExtentMax, int32 InSurfaceType)
			: Location(InLocation)
			, AccumulatedImpulse(InAccumulatedImpulse)
			, Normal(InNormal)
			, Velocity1(InVelocity1)
			, Velocity2(InVelocity2)
			, AngularVelocity1(InAngularVelocity1)
			, AngularVelocity2(InAngularVelocity2)
			, Mass1(InMass1)
			, Mass2(InMass2)
			, Particle(InParticle)
			, Levelset(InLevelset)
		    , ParticleProxy(InParticleProxy)
		    , LevelsetProxy(InLevelsetProxy)
			, BoundingboxVolume(InBoundingboxVolume)
			, BoundingboxExtentMin(InBoundingboxExtentMin)
			, BoundingboxExtentMax(InBoundingboxExtentMax)
			, SurfaceType(InSurfaceType)
		{}

		FCollidingDataExt(const FCollidingData& InCollisionData)
			: Location(InCollisionData.Location)
			, AccumulatedImpulse(InCollisionData.AccumulatedImpulse)
			, Normal(InCollisionData.Normal)
			, Velocity1(InCollisionData.Velocity1)
			, Velocity2(InCollisionData.Velocity2)
			, AngularVelocity1(InCollisionData.AngularVelocity1)
			, AngularVelocity2(InCollisionData.AngularVelocity2)
			, Mass1(InCollisionData.Mass1)
			, Mass2(InCollisionData.Mass2)
			, Particle(InCollisionData.Particle)
			, Levelset(InCollisionData.Levelset)
		    , ParticleProxy(InCollisionData.ParticleProxy)
		    , LevelsetProxy(InCollisionData.LevelsetProxy)
			, BoundingboxVolume((FReal)-1.0)
			, BoundingboxExtentMin((FReal)-1.0)
			, BoundingboxExtentMax((FReal)-1.0)
			, SurfaceType(-1)
		{
		}

		FVec3 Location;
		FVec3 AccumulatedImpulse;
		FVec3 Normal;
		FVec3 Velocity1, Velocity2;
		FVec3 AngularVelocity1, AngularVelocity2;
		FReal Mass1, Mass2;
		FGeometryParticle* Particle;
		FGeometryParticle* Levelset;
		IPhysicsProxyBase* ParticleProxy;
		IPhysicsProxyBase* LevelsetProxy;
		FReal BoundingboxVolume;
		FReal BoundingboxExtentMin, BoundingboxExtentMax;
		int32 SurfaceType;
	};

	/*
	BreakingData passed from the physics solver to subsystems
	*/
	struct FBreakingData
	{
		FBreakingData()
			: Particle(nullptr)
		    , ParticleProxy(nullptr)
			, Location(FVec3((FReal)0.0))
			, Velocity(FVec3((FReal)0.0))
			, AngularVelocity(FVec3((FReal)0.0))
			, Mass((FReal)0.0)
			, BoundingBox(FAABB3(FVec3((FReal)0.0), FVec3((FReal)0.0)))
		{}

		FGeometryParticleHandle* Particle;
		IPhysicsProxyBase* ParticleProxy;
		FVec3 Location;
		FVec3 Velocity;
		FVec3 AngularVelocity;
		FReal Mass;
		Chaos::FAABB3 BoundingBox;
	};

	/*
	BreakingData used in the subsystems
	*/
	struct FBreakingDataExt
	{
		FBreakingDataExt()
			: Location(FVec3((FReal)0.0))
			, Velocity(FVec3((FReal)0.0))
			, AngularVelocity(FVec3((FReal)0.0))
			, Mass((FReal)0.0)
		    , Particle(nullptr)
		    , ParticleProxy(nullptr)
			, BoundingboxVolume((FReal)-1.0)
			, BoundingboxExtentMin((FReal)-1.0)
			, BoundingboxExtentMax((FReal)-1.0)
			, SurfaceType(-1)
		{}

		FBreakingDataExt(FVec3 InLocation
			, FVec3 InVelocity
			, FVec3 InAngularVelocity
			, FReal InMass
			, FGeometryParticleHandle* InParticle
			, IPhysicsProxyBase* InParticleProxy
			, FReal InBoundingboxVolume
			, FReal InBoundingboxExtentMin
			, FReal InBoundingboxExtentMax
			, int32 InSurfaceType)
			: Location(InLocation)
			, Velocity(InVelocity)
			, AngularVelocity(InAngularVelocity)
			, Mass(InMass)
		    , Particle(InParticle)
		    , ParticleProxy(InParticleProxy)
			, BoundingboxVolume(InBoundingboxVolume)
			, BoundingboxExtentMin(InBoundingboxExtentMin)
			, BoundingboxExtentMax(InBoundingboxExtentMax)
			, SurfaceType(InSurfaceType)
		{}

		FBreakingDataExt(const FBreakingData& InBreakingData)
			: Location(InBreakingData.Location)
			, Velocity(InBreakingData.Velocity)
			, AngularVelocity(InBreakingData.AngularVelocity)
			, Mass(InBreakingData.Mass)
		    , Particle(InBreakingData.Particle)
		    , ParticleProxy(InBreakingData.ParticleProxy)
			, BoundingboxVolume((FReal)-1.0)
			, BoundingboxExtentMin((FReal)-1.0)
			, BoundingboxExtentMax((FReal)-1.0)
			, SurfaceType(-1)
		{
		}

		FVec3 Location;
		FVec3 Velocity;
		FVec3 AngularVelocity;
		FReal Mass;
		FGeometryParticleHandle* Particle;
		IPhysicsProxyBase* ParticleProxy;
		FReal BoundingboxVolume;
		FReal BoundingboxExtentMin, BoundingboxExtentMax;
		int32 SurfaceType;

		FVector TransformTranslation;
		FQuat TransformRotation;
		FVector TransformScale;

		FBox BoundingBox;

		// Please don't be tempted to add the code below back. Holding onto a UObject pointer without the GC knowing about it is 
		// not a safe thing to do.
		//UPhysicalMaterial* PhysicalMaterialTest;
		FName PhysicalMaterialName;
	};

	/*
	TrailingData passed from the physics solver to subsystems
	*/
	struct FTrailingData
	{
		FTrailingData()
			: Location(FVec3((FReal)0.0))
			, Velocity(FVec3((FReal)0.0))
			, AngularVelocity(FVec3((FReal)0.0))
			, Mass((FReal)0.0)
			, Particle(nullptr)
		    , ParticleProxy(nullptr)
			, BoundingBox(FAABB3(FVec3((FReal)0.0), FVec3((FReal)0.0)))
		{}

		FTrailingData(FVec3 InLocation, FVec3 InVelocity, FVec3 InAngularVelocity, FReal InMass
			, FGeometryParticleHandle* InParticle, IPhysicsProxyBase* InParticleProxy, Chaos::FAABB3& InBoundingBox)
			: Location(InLocation)
			, Velocity(InVelocity)
			, AngularVelocity(InAngularVelocity)
			, Mass(InMass)
			, Particle(InParticle)
		    , ParticleProxy(InParticleProxy)
			, BoundingBox(InBoundingBox)
		{}

		FVec3 Location;
		FVec3 Velocity;
		FVec3 AngularVelocity;
		FReal Mass;
		FGeometryParticleHandle* Particle;
		IPhysicsProxyBase* ParticleProxy;
		Chaos::FAABB3 BoundingBox;

		friend inline uint32 GetTypeHash(const FTrailingData& Other)
		{
			return ::GetTypeHash(Other.Particle);
		}

		friend bool operator==(const FTrailingData& A, const FTrailingData& B)
		{
			return A.Particle == B.Particle;
		}
	};

	/*
	TrailingData used in subsystems
	*/
	struct FTrailingDataExt
	{
		FTrailingDataExt()
			: Location(FVec3((FReal)0.0))
			, Velocity(FVec3((FReal)0.0))
			, AngularVelocity(FVec3((FReal)0.0))
			, Mass((FReal)0.0)
			, Particle(nullptr)
			, BoundingboxVolume((FReal)-1.0)
			, BoundingboxExtentMin((FReal)-1.0)
			, BoundingboxExtentMax((FReal)-1.0)
			, SurfaceType(-1)
		{}

		FTrailingDataExt(FVec3 InLocation
			, FVec3 InVelocity
			, FVec3 InAngularVelocity
			, FReal InMass
			, FGeometryParticleHandle* InParticle
			, IPhysicsProxyBase* InParticleProxy
			, FReal InBoundingboxVolume
			, FReal InBoundingboxExtentMin
			, FReal InBoundingboxExtentMax
			, int32 InSurfaceType)
			: Location(InLocation)
			, Velocity(InVelocity)
			, AngularVelocity(InAngularVelocity)
			, Mass(InMass)
			, Particle(InParticle)
		    , ParticleProxy(InParticleProxy)
			, BoundingboxVolume(InBoundingboxVolume)
			, BoundingboxExtentMin(InBoundingboxExtentMin)
			, BoundingboxExtentMax(InBoundingboxExtentMax)
			, SurfaceType(InSurfaceType)
		{}

		FTrailingDataExt(const FTrailingData& InTrailingData)
			: Location(InTrailingData.Location)
			, Velocity(InTrailingData.Velocity)
			, AngularVelocity(InTrailingData.AngularVelocity)
			, Mass(InTrailingData.Mass)
			, Particle(InTrailingData.Particle)
		    , ParticleProxy(InTrailingData.ParticleProxy)
			, BoundingboxVolume((FReal)-1.0)
			, BoundingboxExtentMin((FReal)-1.0)
			, BoundingboxExtentMax((FReal)-1.0)
			, SurfaceType(-1)
		{}

		FVec3 Location;
		FVec3 Velocity;
		FVec3 AngularVelocity;
		FReal Mass;
		FGeometryParticleHandle* Particle;
		IPhysicsProxyBase* ParticleProxy;
		//	int32 ParticleIndexMesh; // If ParticleIndex points to a cluster then this index will point to an actual mesh in the cluster
								 // It is important to be able to get extra data from the component
		FReal BoundingboxVolume;
		FReal BoundingboxExtentMin, BoundingboxExtentMax;
		int32 SurfaceType;

		friend inline uint32 GetTypeHash(const FTrailingDataExt& Other)
		{
			return ::GetTypeHash(Other.Particle);
		}

		friend bool operator==(const FTrailingDataExt& A, const FTrailingDataExt& B)
		{
			return A.Particle == B.Particle;
		}
	};

	struct FSleepingData
	{
		FSleepingData()
			: Particle(nullptr)
			, Sleeping(true)
		{}

		FSleepingData(
		    FGeometryParticle* InParticle, bool InSleeping)
			: Particle(InParticle)
			, Sleeping(InSleeping)
		{}

		FGeometryParticle* Particle;
		bool Sleeping;	// if !Sleeping == Awake
	};

	template<class T, int d>
	using TCollisionData UE_DEPRECATED(4.27, "Deprecated. this class is to be deleted, use FCollidingData instead") = FCollidingData;

	template<class T, int d>
	using TCollisionDataExt UE_DEPRECATED(4.27, "Deprecated. this class is to be deleted, use FCollidingDataExt instead") = FCollidingDataExt;

	template<class T, int d>
	using TBreakingData UE_DEPRECATED(4.27, "Deprecated. this class is to be deleted, use FBreakingData instead") = FBreakingData;

	template<class T, int d>
	using TBreakingDataExt UE_DEPRECATED(4.27, "Deprecated. this class is to be deleted, use FBreakingDataExt instead") = FBreakingDataExt;

	template<class T, int d>
	using TTrailingData UE_DEPRECATED(4.27, "Deprecated. this class is to be deleted, use FTrailingData instead") = FTrailingData;

	template<class T, int d>
	using TTrailingDataExt UE_DEPRECATED(4.27, "Deprecated. this class is to be deleted, use FTrailingDataExt instead") = FTrailingDataExt;

	template<class T, int d>
	using TSleepingData UE_DEPRECATED(4.27, "Deprecated. this class is to be deleted, use FSleepingData instead") = FSleepingData;
}


