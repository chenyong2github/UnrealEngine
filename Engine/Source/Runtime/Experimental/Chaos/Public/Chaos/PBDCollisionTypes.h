// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Vector.h"
#include "CoreMinimal.h"
#include "Box.h"

class UPhysicalMaterial;

namespace Chaos
{
/**/
// @todo(ccaulfield): should these be held SOA style in the PBDCollisionConstraint?
template<class T, int d>
struct TRigidBodyContactConstraint
{
	TRigidBodyContactConstraint() : AccumulatedImpulse(0.f) {}
	int32 ParticleIndex, LevelsetIndex;
	TVector<T, d> Normal;
	TVector<T, d> Location;
	T Phi;
	TVector<T, d> AccumulatedImpulse;

	FString ToString() const
	{
		return FString::Printf(TEXT("ParticleIndex:%d, LevelsetIndex:%d, Normal:%s, Location:%s, Phi:%f, AccumulatedImpulse:%s"), ParticleIndex, LevelsetIndex, *Normal.ToString(), *Location.ToString(), Phi, *AccumulatedImpulse.ToString());
	}
};

template<class T, int d>
struct TRigidBodyContactConstraintPGS
{
	TRigidBodyContactConstraintPGS() : AccumulatedImpulse(0.f) {}
	int32 ParticleIndex, LevelsetIndex;
	TArray<TVector<T, d>> Normal;
	TArray<TVector<T, d>> Location;
	TArray<T> Phi;
	TVector<T, d> AccumulatedImpulse;
};

/**
 * Collision event data stored for use by other systems (e.g. Niagara, gameplay events)
 */
template<class T, int d>
struct TCollisionData
{
	TCollisionData()
		: Location(TVector<T, d>((T)0.0))
		, AccumulatedImpulse(TVector<T, d>((T)0.0))
		, Normal(TVector<T, d>((T)0.0))
		, Velocity1(TVector<T, d>((T)0.0))
		, Velocity2(TVector<T, d>((T)0.0))
		, AngularVelocity1(TVector<T, d>((T)0.0))
		, AngularVelocity2(TVector<T, d>((T)0.0))
		, Mass1((T)0.0)
		, Mass2((T)0.0)
		, PenetrationDepth((T)0.0)
		, ParticleIndex(INDEX_NONE)
		, LevelsetIndex(INDEX_NONE)
		, ParticleIndexMesh(INDEX_NONE)
		, LevelsetIndexMesh(INDEX_NONE)
	{}

	TCollisionData(TVector<T, d> InLocation
		, TVector<T, d> InAccumulatedImpulse
		, TVector<T, d> InNormal
		, TVector<T, d> InVelocity1
		, TVector<T, d> InVelocity2
		, TVector<T, d> InAngularVelocity1
		, TVector<T, d> InAngularVelocity2
		, T InMass1
		, T InMass2
		, T InPenetrationDepth
		, int32 InParticleIndex
		, int32 InLevelsetIndex
		, int32 InParticleIndexMesh
		, int32 InLevelsetIndexMesh)
		: Location(InLocation)
		, AccumulatedImpulse(InAccumulatedImpulse)
		, Normal(InNormal)
		, Velocity1(InVelocity1)
		, Velocity2(InVelocity2)
		, AngularVelocity1(InAngularVelocity1)
		, AngularVelocity2(InAngularVelocity2)
		, Mass1(InMass1)
		, Mass2(InMass2)
		, PenetrationDepth(InPenetrationDepth)
		, ParticleIndex(InParticleIndex)
		, LevelsetIndex(InLevelsetIndex)
		, ParticleIndexMesh(InParticleIndexMesh)
		, LevelsetIndexMesh(InLevelsetIndexMesh)
	{}

	TVector<T, d> Location;
	TVector<T, d> AccumulatedImpulse;
	TVector<T, d> Normal;
	TVector<T, d> Velocity1, Velocity2;
	TVector<T, d> AngularVelocity1, AngularVelocity2;
	T Mass1, Mass2;
	T PenetrationDepth;
	int32 ParticleIndex, LevelsetIndex;
	int32 ParticleIndexMesh, LevelsetIndexMesh; // If ParticleIndex points to a cluster then this index will point to an actual mesh in the cluster
												// It is important to be able to get extra data from the component

};

/*
CollisionData used in the subsystems
*/
template<class T, int d>
struct TCollisionDataExt
{
	TCollisionDataExt()
		: Location(TVector<T, d>((T)0.0))
		, AccumulatedImpulse(TVector<T, d>((T)0.0))
		, Normal(TVector<T, d>((T)0.0))
		, Velocity1(TVector<T, d>((T)0.0))
		, Velocity2(TVector<T, d>((T)0.0))
		, AngularVelocity1(TVector<T, d>((T)0.0))
		, AngularVelocity2(TVector<T, d>((T)0.0))
		, Mass1((T)0.0)
		, Mass2((T)0.0)
		, ParticleIndex(INDEX_NONE)
		, LevelsetIndex(INDEX_NONE)
		, ParticleIndexMesh(INDEX_NONE)
		, LevelsetIndexMesh(INDEX_NONE)
		, BoundingboxVolume((T)-1.0)
		, BoundingboxExtentMin((T)-1.0)
		, BoundingboxExtentMax((T)-1.0)
		, SurfaceType(-1)
	{}

	TCollisionDataExt(TVector<T, d> InLocation
		, TVector<T, d> InAccumulatedImpulse
		, TVector<T, d> InNormal
		, TVector<T, d> InVelocity1
		, TVector<T, d> InVelocity2
		, TVector<T, d> InAngularVelocity1
		, TVector<T, d> InAngularVelocity2
		, T InMass1
		, T InMass2
		, int32 InParticleIndex
		, int32 InLevelsetIndex
		, int32 InParticleIndexMesh
		, int32 InLevelsetIndexMesh
		, float InBoundingboxVolume
		, float InBoundingboxExtentMin
		, float InBoundingboxExtentMax
		, int32 InSurfaceType)
		: Location(InLocation)
		, AccumulatedImpulse(InAccumulatedImpulse)
		, Normal(InNormal)
		, Velocity1(InVelocity1)
		, Velocity2(InVelocity2)
		, AngularVelocity1(InAngularVelocity1)
		, AngularVelocity2(InAngularVelocity2)
		, Mass1(InMass1)
		, Mass2(InMass2)
		, ParticleIndex(InParticleIndex)
		, LevelsetIndex(InLevelsetIndex)
		, ParticleIndexMesh(InParticleIndexMesh)
		, LevelsetIndexMesh(InLevelsetIndexMesh)
		, BoundingboxVolume(InBoundingboxVolume)
		, BoundingboxExtentMin(InBoundingboxExtentMin)
		, BoundingboxExtentMax(InBoundingboxExtentMax)
		, SurfaceType(InSurfaceType)
	{}

	TCollisionDataExt(const TCollisionData<T, d>& InCollisionData)
		: Location(InCollisionData.Location)
		, AccumulatedImpulse(InCollisionData.AccumulatedImpulse)
		, Normal(InCollisionData.Normal)
		, Velocity1(InCollisionData.Velocity1)
		, Velocity2(InCollisionData.Velocity2)
		, AngularVelocity1(InCollisionData.AngularVelocity1)
		, AngularVelocity2(InCollisionData.AngularVelocity2)
		, Mass1(InCollisionData.Mass1)
		, Mass2(InCollisionData.Mass2)
		, ParticleIndex(InCollisionData.ParticleIndex)
		, LevelsetIndex(InCollisionData.LevelsetIndex)
		, ParticleIndexMesh(InCollisionData.ParticleIndexMesh)
		, LevelsetIndexMesh(InCollisionData.LevelsetIndexMesh)
		, BoundingboxVolume((T)-1.0)
		, BoundingboxExtentMin((T)-1.0)
		, BoundingboxExtentMax((T)-1.0)
		, SurfaceType(-1)
	{}

	TVector<T, d> Location;
	TVector<T, d> AccumulatedImpulse;
	TVector<T, d> Normal;
	TVector<T, d> Velocity1, Velocity2;
	TVector<T, d> AngularVelocity1, AngularVelocity2;
	T Mass1, Mass2;
	int32 ParticleIndex, LevelsetIndex;
	int32 ParticleIndexMesh, LevelsetIndexMesh;
	float BoundingboxVolume;
	float BoundingboxExtentMin, BoundingboxExtentMax;
	int32 SurfaceType;
};

/*
BreakingData passed from the physics solver to subsystems
*/
template<class T, int d>
struct TBreakingData
{
	TBreakingData()
		: Location(TVector<T, d>((T)0.0))
		, Velocity(TVector<T, d>((T)0.0))
		, AngularVelocity(TVector<T, d>((T)0.0))
		, Mass((T)0.0)
		, ParticleIndex(INDEX_NONE)
		, ParticleIndexMesh(INDEX_NONE)
		, BoundingBox(TBox<T, d>(TVector<T, d>((T)0.0), TVector<T, d>((T)0.0)))
	{}

	TBreakingData(TVector<T, d> InLocation
		, TVector<T, d> InVelocity
		, TVector<T, d> InAngularVelocity
		, T InMass
		, int32 InParticleIndex
		, int32 InParticleIndexMesh
		, Chaos::TBox<T, d>& InBoundingBox)
		: Location(InLocation)
		, Velocity(InVelocity)
		, AngularVelocity(InAngularVelocity)
		, Mass(InMass)
		, ParticleIndex(InParticleIndex)
		, ParticleIndexMesh(InParticleIndexMesh)
	    , BoundingBox(InBoundingBox)
	{}

	TVector<T, d> Location;
	TVector<T, d> Velocity;
	TVector<T, d> AngularVelocity;
	T Mass;
	int32 ParticleIndex;
	int32 ParticleIndexMesh; // If ParticleIndex points to a cluster then this index will point to an actual mesh in the cluster
							 // It is important to be able to get extra data from the component
	Chaos::TBox<T, d> BoundingBox;
};



/*
BreakingData used in the subsystems
*/
template<class T, int d>
struct TBreakingDataExt
{
	TBreakingDataExt()
		: Location(TVector<T, d>((T)0.0))
		, Velocity(TVector<T, d>((T)0.0))
		, AngularVelocity(TVector<T, d>((T)0.0))
		, Mass((T)0.0)
		, ParticleIndex(INDEX_NONE)
		, ParticleIndexMesh(INDEX_NONE)
		, BoundingboxVolume((T)-1.0)
		, BoundingboxExtentMin((T)-1.0)
		, BoundingboxExtentMax((T)-1.0)
		, SurfaceType(-1)
	{}

	TBreakingDataExt(TVector<T, d> InLocation
		, TVector<T, d> InVelocity
		, TVector<T, d> InAngularVelocity
		, T InMass
		, int32 InParticleIndex
		, int32 InParticleIndexMesh
		, float InBoundingboxVolume
		, float InBoundingboxExtentMin
		, float InBoundingboxExtentMax
		, int32 InSurfaceType)
		: Location(InLocation)
		, Velocity(InVelocity)
		, AngularVelocity(InAngularVelocity)
		, Mass(InMass)
		, ParticleIndex(InParticleIndex)
		, ParticleIndexMesh(InParticleIndexMesh)
		, BoundingboxVolume(InBoundingboxVolume)
		, BoundingboxExtentMin(InBoundingboxExtentMin)
		, BoundingboxExtentMax(InBoundingboxExtentMax)
		, SurfaceType(InSurfaceType)
	{}

	TBreakingDataExt(const TBreakingData<float, 3>& InBreakingData)
		: Location(InBreakingData.Location)
		, Velocity(InBreakingData.Velocity)
		, AngularVelocity(InBreakingData.AngularVelocity)
		, Mass(InBreakingData.Mass)
		, ParticleIndex(InBreakingData.ParticleIndex)
		, ParticleIndexMesh(InBreakingData.ParticleIndexMesh)
		, BoundingboxVolume((T)-1.0)
		, BoundingboxExtentMin((T)-1.0)
		, BoundingboxExtentMax((T)-1.0)
		, SurfaceType(-1)
	{}

	TVector<T, d> Location;
	TVector<T, d> Velocity;
	TVector<T, d> AngularVelocity;
	T Mass;
	int32 ParticleIndex;
	int32 ParticleIndexMesh; // If ParticleIndex points to a cluster then this index will point to an actual mesh in the cluster
							 // It is important to be able to get extra data from the component
	float BoundingboxVolume;
	float BoundingboxExtentMin, BoundingboxExtentMax;
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
template<class T, int d>
struct TTrailingData
{
	TTrailingData()
		: Location(TVector<T, d>((T)0.0))
		, Velocity(TVector<T, d>((T)0.0))
		, AngularVelocity(TVector<T, d>((T)0.0))
		, Mass((T)0.0)
		, ParticleIndex(INDEX_NONE)
		, ParticleIndexMesh(INDEX_NONE)
		, BoundingBox(TBox<T, d>(TVector<T, d>((T)0.0), TVector<T, d>((T)0.0)))
	{}

	TTrailingData(TVector<T, d> InLocation
		, TVector<T, d> InVelocity
		, TVector<T, d> InAngularVelocity
		, T InMass
		, int32 InParticleIndex
		, int32 InParticleIndexMesh
		, float InBoundingboxVolume
		, float InBoundingboxExtentMin
		, float InBoundingboxExtentMax
		, int32 InSurfaceType
		, Chaos::TBox<T, d>& InBoundingBox)
		: Location(InLocation)
		, Velocity(InVelocity)
		, AngularVelocity(InAngularVelocity)
		, Mass(InMass)
		, ParticleIndex(InParticleIndex)
		, ParticleIndexMesh(InParticleIndexMesh)
		, BoundingBox(InBoundingBox)
	{}

	TVector<T, d> Location;
	TVector<T, d> Velocity;
	TVector<T, d> AngularVelocity;
	T Mass;
	int32 ParticleIndex;
	int32 ParticleIndexMesh; // If ParticleIndex points to a cluster then this index will point to an actual mesh in the cluster
							 // It is important to be able to get extra data from the component
	Chaos::TBox<T, d> BoundingBox;

	friend inline uint32 GetTypeHash(const TTrailingData& Other)
	{
		return ::GetTypeHash(Other.ParticleIndex);
	}

	friend bool operator==(const TTrailingData& A, const TTrailingData& B)
	{
		return A.ParticleIndex == B.ParticleIndex;
	}
};

/*
TrailingData used in subsystems
*/
template<class T, int d>
struct TTrailingDataExt
{
	TTrailingDataExt()
		: Location(TVector<T, d>((T)0.0))
		, Velocity(TVector<T, d>((T)0.0))
		, AngularVelocity(TVector<T, d>((T)0.0))
		, Mass((T)0.0)
		, ParticleIndex(INDEX_NONE)
		, ParticleIndexMesh(INDEX_NONE)
		, BoundingboxVolume((T)-1.0)
		, BoundingboxExtentMin((T)-1.0)
		, BoundingboxExtentMax((T)-1.0)
		, SurfaceType(-1)
	{}

	TTrailingDataExt(TVector<T, d> InLocation
		, TVector<T, d> InVelocity
		, TVector<T, d> InAngularVelocity
		, T InMass
		, int32 InParticleIndex
		, int32 InParticleIndexMesh
		, float InBoundingboxVolume
		, float InBoundingboxExtentMin
		, float InBoundingboxExtentMax
		, int32 InSurfaceType)
		: Location(InLocation)
		, Velocity(InVelocity)
		, AngularVelocity(InAngularVelocity)
		, Mass(InMass)
		, ParticleIndex(InParticleIndex)
		, ParticleIndexMesh(InParticleIndexMesh)
		, BoundingboxVolume(InBoundingboxVolume)
		, BoundingboxExtentMin(InBoundingboxExtentMin)
		, BoundingboxExtentMax(InBoundingboxExtentMax)
		, SurfaceType(InSurfaceType)
	{}

	TTrailingDataExt(const TTrailingData<float, 3>& InTrailingData)
		: Location(InTrailingData.Location)
		, Velocity(InTrailingData.Velocity)
		, AngularVelocity(InTrailingData.AngularVelocity)
		, Mass(InTrailingData.Mass)
		, ParticleIndex(InTrailingData.ParticleIndex)
		, ParticleIndexMesh(InTrailingData.ParticleIndexMesh)
		, BoundingboxVolume((T)-1.0)
		, BoundingboxExtentMin((T)-1.0)
		, BoundingboxExtentMax((T)-1.0)
		, SurfaceType(-1)
	{}

	TVector<T, d> Location;
	TVector<T, d> Velocity;
	TVector<T, d> AngularVelocity;
	T Mass;
	int32 ParticleIndex;
	int32 ParticleIndexMesh; // If ParticleIndex points to a cluster then this index will point to an actual mesh in the cluster
							 // It is important to be able to get extra data from the component
	float BoundingboxVolume;
	float BoundingboxExtentMin, BoundingboxExtentMax;
	int32 SurfaceType;

	friend inline uint32 GetTypeHash(const TTrailingDataExt& Other)
	{
		return ::GetTypeHash(Other.ParticleIndex);
	}

	friend bool operator==(const TTrailingDataExt& A, const TTrailingDataExt& B)
	{
		return A.ParticleIndex == B.ParticleIndex;
	}
};

}
