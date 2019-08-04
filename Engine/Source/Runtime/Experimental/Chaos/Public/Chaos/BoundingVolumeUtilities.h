// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/Box.h"
#include "Chaos/Defines.h"
#include "Chaos/Framework/Parallel.h"
#include "Chaos/GeometryParticles.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/Particles.h"
#include "Chaos/Sphere.h"
#include "Chaos/Transform.h"

namespace Chaos
{
template<class OBJECT_ARRAY>
bool HasBoundingBox(const OBJECT_ARRAY& Objects, const int32 i)
{
	return Objects[i]->HasBoundingBox();
}

template<class T, int d>
bool HasBoundingBox(const TParticles<T, d>& Objects, const int32 i)
{
	return true;
}

template<class T, int d>
bool HasBoundingBox(const TGeometryParticles<T, d>& Objects, const int32 i)
{
	return Objects.Geometry(i)->HasBoundingBox();
}

template<class T, int d>
bool HasBoundingBox(const TPBDRigidParticles<T, d>& Objects, const int32 i)
{
	if (Objects.Geometry(i))
	{
		return Objects.Geometry(i)->HasBoundingBox();
	}
	return Objects.CollisionParticles(i) != nullptr && Objects.CollisionParticles(i)->Size() > 0;
}

template<typename THandle>
bool HasBoundingBox(const THandle& Handle)
{
	if (Handle.Geometry())
	{
		return Handle.Geometry()->HasBoundingBox();
	}
	
	if (const auto PBDRigid = Handle.AsDynamic())
	{
		return PBDRigid->CollisionParticles() && PBDRigid->CollisionParticles()->Size() > 0;
	}
	return false;
}

template<class OBJECT_ARRAY, class T, int d>
const TBox<T, d>& GetWorldSpaceBoundingBox(const OBJECT_ARRAY& Objects, const int32 i, const TMap<int32, TBox<T, d>>& WorldSpaceBoxes)
{
	return Objects[i]->BoundingBox();
}

template<class T, int d>
const TBox<T, d>& GetWorldSpaceBoundingBox(const TParticles<T, d>& Objects, const int32 i, const TMap<int32, TBox<T, d>>& WorldSpaceBoxes)
{
	return WorldSpaceBoxes[i];
}

template<class T, int d>
const TBox<T, d>& GetWorldSpaceBoundingBox(const TGeometryParticles<T, d>& Objects, const int32 i, const TMap<int32, TBox<T, d>>& WorldSpaceBoxes)
{
	return GetWorldSpaceBoundingBox(static_cast<const TParticles<T, d>&>(Objects), i, WorldSpaceBoxes);
}

template<class T, int d>
const TBox<T, d>& GetWorldSpaceBoundingBox(const TPBDRigidParticles<T, d>& Objects, const int32 i, const TMap<int32, TBox<T, d>>& WorldSpaceBoxes)
{
	return GetWorldSpaceBoundingBox(static_cast<const TParticles<T, d>&>(Objects), i, WorldSpaceBoxes);
}

template<class T, int d>
TBox<T, d> ComputeWorldSpaceBoundingBox(const TParticles<T, d>& Objects, const int32 i)
{
	return TBox<T, d>(Objects.X(i), Objects.X(i));
}

template<class T, int d>
TBox<T, d> ComputeWorldSpaceBoundingBox(const TGeometryParticles<T, d>& Objects, const int32 i)
{
	TRigidTransform<T, d> LocalToWorld(Objects.X(i), Objects.R(i));
	const auto& LocalBoundingBox = Objects.Geometry(i)->BoundingBox();
	return LocalBoundingBox.TransformedBox(LocalToWorld);
}

template<class T, int d>
TBox<T, d> ComputeWorldSpaceBoundingBox(const TPBDRigidParticles<T, d>& Objects, const int32 i)
{
	TRigidTransform<T, d> LocalToWorld(Objects.P(i), Objects.Q(i));
	if (Objects.Geometry(i))
	{
		const auto& LocalBoundingBox = Objects.Geometry(i)->BoundingBox();
		return LocalBoundingBox.TransformedBox(LocalToWorld);
	}
	check(Objects.CollisionParticles(i) && Objects.CollisionParticles(i)->Size());
	TBox<T, d> LocalBoundingBox(Objects.CollisionParticles(i)->X(0), Objects.CollisionParticles(i)->X(0));
	for (uint32 j = 1; j < Objects.CollisionParticles(i)->Size(); ++j)
	{
		LocalBoundingBox.GrowToInclude(Objects.CollisionParticles(i)->X(j));
	}
	return LocalBoundingBox.TransformedBox(LocalToWorld);
}

template<typename THandle>
TBox<typename THandle::TType, THandle::D> ComputeWorldSpaceBoundingBox(const THandle& Handle)
{
	using T = typename THandle::TType;
	constexpr int d = THandle::D;

	const auto PBDRigid = Handle.AsDynamic();
	TRigidTransform<T, d> LocalToWorld = PBDRigid ? TRigidTransform<T,d>(PBDRigid->P(), PBDRigid->Q()) : TRigidTransform<T,d>(Handle.X(), Handle.R());
	if (Handle.Geometry())
	{
		const auto& LocalBoundingBox = Handle.Geometry()->BoundingBox();
		return LocalBoundingBox.TransformedBox(LocalToWorld);
	}

	check(PBDRigid);
	check(PBDRigid->CollisionParticles() && PBDRigid->CollisionParticles()->Size());
	TBox<T, d> LocalBoundingBox(PBDRigid->CollisionParticles()->X(0), PBDRigid->CollisionParticles()->X(0));
	for (uint32 j = 1; j < PBDRigid->CollisionParticles()->Size(); ++j)
	{
		LocalBoundingBox.GrowToInclude(PBDRigid->CollisionParticles()->X(j));
	}
	return LocalBoundingBox.TransformedBox(LocalToWorld);
}

template<typename OBJECT_ARRAY, typename T, int d>
const TBox<T, d> ComputeGlobalBoxAndSplitAxis(const OBJECT_ARRAY& Objects, const TArray<int32>& AllObjects, const TMap<int32, TBox<T, d>>& WorldSpaceBoxes, bool bAllowMultipleSplitting, int32& OutAxis)
{
	TBox<T, d> GlobalBox = GetWorldSpaceBoundingBox(Objects, AllObjects[0], WorldSpaceBoxes);
	for (int32 i = 1; i < AllObjects.Num(); ++i)
	{
		GlobalBox.GrowToInclude(GetWorldSpaceBoundingBox(Objects, AllObjects[i], WorldSpaceBoxes));
	}
	int32 Axis = 0;
	TVector<T, d> GlobalExtents = GlobalBox.Extents();
	if (GlobalExtents[2] > GlobalExtents[0] && GlobalExtents[2] > GlobalExtents[1])
	{
		Axis = 2;
	}
	else if (GlobalExtents[1] > GlobalExtents[0])
	{
		Axis = 1;
	}
	if (bAllowMultipleSplitting && GlobalExtents[Axis] < GlobalExtents[(Axis + 1) % 3] * 1.25 && GlobalExtents[Axis] < GlobalExtents[(Axis + 2) % 3] * 1.25 && AllObjects.Num() > 4 * MIN_NUM_OBJECTS)
	{
		Axis = -1;
	}

	OutAxis = Axis;
	return GlobalBox;
}

template<typename T, int d>
const TBox<T, d> ComputeGlobalBoxAndSplitAxis(const TParticles<T,d>& Objects, const TArray<int32>& AllObjects, const TMap<int32, TBox<T, d>>& WorldSpaceBoxes, bool bAllowMultipleSplitting, int32& OutAxis)
{
	//simple particles means we can split more efficiently
	TPair<int32, int32> Counts[d];

	for (int32 i = 0; i < d; ++i)
	{
		Counts[i].Key = 0;
		Counts[i].Value = 0;
	};

	auto CountLambda = [&](const TVector<T, d>& Point)
	{
		for (int32 i = 0; i < d; ++i)
		{
			Counts[i].Key += Point[i] > 0 ? 0 : 1;
			Counts[i].Value += Point[i] > 0 ? 1 : 0;
		};
	};

	TBox<T, d> GlobalBox = GetWorldSpaceBoundingBox(Objects, AllObjects[0], WorldSpaceBoxes);
	CountLambda(GlobalBox.Center());
	for (int32 i = 1; i < AllObjects.Num(); ++i)
	{
		TBox<T, d> PtBox = GetWorldSpaceBoundingBox(Objects, AllObjects[i], WorldSpaceBoxes);
		GlobalBox.GrowToInclude(PtBox);
		CountLambda(PtBox.Center());
	}

	//we pick the axis that gives us the most culled even in the case when it goes in the wrong direction (i.e the biggest min)
	int32 BestAxis = 0;
	int32 MaxCulled = 0;
	for (int32 Axis = 0; Axis < d; ++Axis)
	{
		int32 CulledWorstCase = FMath::Min(Counts[Axis].Key, Counts[Axis].Value);
		if (CulledWorstCase > MaxCulled)
		{
			MaxCulled = CulledWorstCase;
			BestAxis = Axis;
		}
	}
	
	//todo(ocohen): use multi split when CulledWorstCase is similar for every axis

	OutAxis = BestAxis;
	return GlobalBox;
}

template<class OBJECT_ARRAY, class T, int d>
void ComputeAllWorldSpaceBoundingBoxes(const OBJECT_ARRAY& Objects, const TArray<int32>& AllObjects, const bool bUseVelocity, const T Dt, TMap<int32, TBox<T, d>>& WorldSpaceBoxes)
{
	check(!bUseVelocity);
}

template<class T, int d>
void ComputeAllWorldSpaceBoundingBoxes(const TParticles<T, d>& Objects, const TArray<int32>& AllObjects, const bool bUseVelocity, const T Dt, TMap<int32, TBox<T, d>>& WorldSpaceBoxes)
{
	check(!bUseVelocity);
	WorldSpaceBoxes.Reserve(AllObjects.Num());
	//PhysicsParallelFor(AllObjects.Num(), [&](int32 i) {
	for (int32 i : AllObjects)
	{
		WorldSpaceBoxes.FindOrAdd(i) = ComputeWorldSpaceBoundingBox(Objects, i);
	}
	//});
}

template<class T, int d>
void ComputeAllWorldSpaceBoundingBoxes(const TGeometryParticles<T, d>& Objects, const TArray<int32>& AllObjects, const bool bUseVelocity, const T Dt, TMap<int32, TBox<T, d>>& WorldSpaceBoxes)
{
	check(!bUseVelocity);
	WorldSpaceBoxes.Reserve(AllObjects.Num());
	//PhysicsParallelFor(AllObjects.Num(), [&](int32 i) {
	for (int32 i : AllObjects)
	{
		WorldSpaceBoxes.FindOrAdd(i) = ComputeWorldSpaceBoundingBox(Objects, i);
	}
	//});
}

extern float MinBoundsThickness;
extern float BoundsThicknessMultiplier;

template <typename T, int d>
TVector<T, d> ComputeThickness(const TPBDRigidParticles<T, d>& InParticles, T Dt, int32 BodyIndex)
{
	TVector<T, d> AbsVelocity = InParticles.V(BodyIndex).GetAbs();
	for (int i = 0; i < d; ++i)
	{
		AbsVelocity[i] = FMath::Max(MinBoundsThickness, AbsVelocity[i] * Dt * BoundsThicknessMultiplier);//todo(ocohen): ignoring MThickness for now
	}

	return AbsVelocity;
}

template <typename THandle, typename T>
TVector<T, THandle::D> ComputeThickness(const THandle& PBDRigid, T Dt)
{
	auto AbsVelocity = PBDRigid.V().GetAbs();
	for (int i = 0; i < THandle::D; ++i)
	{
		AbsVelocity[i] = FMath::Max(MinBoundsThickness, AbsVelocity[i] * Dt * BoundsThicknessMultiplier);//todo(ocohen): ignoring MThickness for now
	}

	return AbsVelocity;
}

template<class T, int d>
void ComputeAllWorldSpaceBoundingBoxes(const TPBDRigidParticles<T, d>& Objects, const TArray<int32>& AllObjects, const bool bUseVelocity, const T Dt, TMap<int32, TBox<T, d>>& WorldSpaceBoxes)
{
	WorldSpaceBoxes.Reserve(AllObjects.Num());
	//PhysicsParallelFor(AllObjects.Num(), [&](int32 i) {
	for (int32 i = 0; i < AllObjects.Num(); ++i)
	{
		const int32 BodyIndex = AllObjects[i];
		TBox<T, d>& WorldSpaceBox = WorldSpaceBoxes.FindOrAdd(BodyIndex);
		WorldSpaceBox = ComputeWorldSpaceBoundingBox(Objects, BodyIndex);
		if (bUseVelocity)
		{
			WorldSpaceBox.ThickenSymmetrically(ComputeThickness(Objects, Dt, BodyIndex));
		}
	}
	//});
}

//todo: how do we protect ourselves and make it const?
template<typename ParticleView, typename T, int d>
void ComputeAllWorldSpaceBoundingBoxes(const ParticleView& Particles, const TArray<bool>& RequiresBounds, const bool bUseVelocity, const T Dt, TArray<TBox<T, d>>& WorldSpaceBoxes)
{
	WorldSpaceBoxes.AddUninitialized(Particles.Num());
	ParticlesParallelFor(Particles, [&RequiresBounds, &WorldSpaceBoxes, bUseVelocity, Dt](const auto& Particle, int32 Index)
	{
		if (RequiresBounds[Index])
		{
			WorldSpaceBoxes[Index] = ComputeWorldSpaceBoundingBox(Particle);
			if (bUseVelocity)
			{
				if (const auto PBDRigid = Particle.AsDynamic())
				{
					WorldSpaceBoxes.Last().ThickenSymmetrically(ComputeThickness(*PBDRigid, Dt));
				}
			}
		}
	});
}

template<class OBJECT_ARRAY>
int32 GetObjectCount(const OBJECT_ARRAY& Objects)
{
	return Objects.Num();
}

template<class T, int d>
int32 GetObjectCount(const TParticles<T, d>& Objects)
{
	return Objects.Size();
}

template<class T, int d>
int32 GetObjectCount(const TGeometryParticles<T, d>& Objects)
{
	return GetObjectCount(static_cast<const TParticles<T, d>&>(Objects));
}

template<class T, int d>
int32 GetObjectCount(const TPBDRigidParticles<T, d>& Objects)
{
	return GetObjectCount(static_cast<const TParticles<T, d>&>(Objects));
}

template<class OBJECT_ARRAY>
bool IsDisabled(const OBJECT_ARRAY& Objects, const uint32 Index)
{
	return false;
}

template<class T, int d>
bool IsDisabled(const TGeometryParticles<T, d>& Objects, const uint32 Index)
{
	return false;
}

template<class T, int d>
bool IsDisabled(const TPBDRigidParticles<T, d>& Objects, const uint32 Index)
{
	return Objects.Disabled(Index);
}
}
