// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/ISpatialAcceleration.h"
#include "Chaos/GeometryParticles.h"

#include "ImplicitObjectTransformed.h"
#include "ChaosArchive.h"

namespace Chaos
{

template<class OBJECT_ARRAY, class LEAF_TYPE, class T, int d>
class TBoundingVolumeHierarchy;

template<class T, int d>
class TImplicitObjectUnion : public FImplicitObject
{
  public:

	using FImplicitObject::GetTypeName;

	CHAOS_API TImplicitObjectUnion(TArray<TUniquePtr<FImplicitObject>>&& Objects, const TArray<int32>& OriginalParticleLookupHack = TArray<int32>());
	TImplicitObjectUnion(const TImplicitObjectUnion<T, d>& Other) = delete;
	TImplicitObjectUnion(TImplicitObjectUnion<T, d>&& Other);
	virtual ~TImplicitObjectUnion();

	FORCEINLINE static constexpr EImplicitObjectType StaticType()
	{
		return ImplicitObjectType::Union;
	}

	virtual T PhiWithNormal(const TVector<T, d>& x, TVector<T, d>& Normal) const override
	{
		T Phi = TNumericLimits<T>::Max();
		bool NeedsNormalize = false;
		for (int32 i = 0; i < MObjects.Num(); ++i)
		{
			if(!ensure(MObjects[i]))
			{
				continue;
			}
			TVector<T, d> NextNormal;
			T NextPhi = MObjects[i]->PhiWithNormal(x, NextNormal);
			if (NextPhi < Phi)
			{
				Phi = NextPhi;
				Normal = NextNormal;
				NeedsNormalize = false;
			}
			else if (NextPhi == Phi)
			{
				Normal += NextNormal;
				NeedsNormalize = true;
			}
		}
		if(NeedsNormalize)
		{
			Normal.Normalize();
		}
		return Phi;
	}

	virtual const TAABB<T, d>& BoundingBox() const override { return MLocalBoundingBox; }

	virtual void AccumulateAllImplicitObjects(TArray<Pair<const FImplicitObject*, TRigidTransform<T, d>>>& Out, const TRigidTransform<T, d>& ParentTM) const
	{
		for (const TUniquePtr<FImplicitObject>& Object : MObjects)
		{
			Object->AccumulateAllImplicitObjects(Out, ParentTM);
		}
	}

	virtual void AccumulateAllSerializableImplicitObjects(TArray<Pair<TSerializablePtr<FImplicitObject>, TRigidTransform<T, d>>>& Out, const TRigidTransform<T, d>& ParentTM, TSerializablePtr<FImplicitObject> This) const
	{
		AccumulateAllSerializableImplicitObjectsHelper(Out, ParentTM);
	}

	void AccumulateAllSerializableImplicitObjectsHelper(TArray<Pair<TSerializablePtr<FImplicitObject>, TRigidTransform<T, d>>>& Out, const TRigidTransform<T, d>& ParentTM) const
	{
		for (const TUniquePtr<FImplicitObject>& Object : MObjects)
		{
			Object->AccumulateAllSerializableImplicitObjects(Out, ParentTM, MakeSerializable(Object));
		}
	}

	virtual void FindAllIntersectingObjects(TArray < Pair<const FImplicitObject*, TRigidTransform<T, d>>>& Out, const TAABB<T, d>& LocalBounds) const;
	TArray<int32> FindAllIntersectingChildren(const TAABB<T, d>& LocalBounds) const;
	TArray<int32> FindAllIntersectingChildren(const TSpatialRay<T, d>& LocalRay) const;
	virtual void CacheAllImplicitObjects();

	virtual bool Raycast(const TVector<T, d>& StartPoint, const TVector<T, d>& Dir, const T Length, const T Thickness, T& OutTime, TVector<T, d>& OutPosition, TVector<T, d>& OutNormal, int32& OutFaceIndex) const override
	{
		T MinTime = 0;	//initialization not needed, but doing it to avoid warning
		bool bFound = false;

		for (const TUniquePtr<FImplicitObject>& Obj : MObjects)
		{
			TVector<T, d> Position;
			TVector<T, d> Normal;
			T Time;
			int32 FaceIdx;
			if (Obj->Raycast(StartPoint, Dir, Length, Thickness, Time, Position, Normal, FaceIdx))
			{
				if (!bFound || Time < MinTime)
				{
					MinTime = Time;
					OutTime = Time;
					OutPosition = Position;
					OutNormal = Normal;
					OutFaceIndex = FaceIdx;
					bFound = true;
				}
			}
		}

		return bFound;
	}

	virtual bool Overlap(const TVector<T, d>& Point, const T Thickness) const override
	{
		for (const TUniquePtr<FImplicitObject>& Obj : MObjects)
		{
			if (Obj->Overlap(Point, Thickness))
			{
				return true;
			}
		}

		return false;
	}

	virtual void Serialize(FChaosArchive& Ar) override;

	virtual bool IsValidGeometry() const
	{
		bool bValid = FImplicitObject::IsValidGeometry();
		bValid = bValid && MObjects.Num();
		return bValid;
	}

	const TArray<TUniquePtr<FImplicitObject>>& GetObjects() const { return MObjects; }

	virtual uint32 GetTypeHash() const override
	{
		uint32 Result = 0;

		// Union hash is just the hash of all internal objects
		for(const TUniquePtr<FImplicitObject>& InnerObj : MObjects)
		{
			Result = HashCombine(Result, InnerObj->GetTypeHash());
		}

		return Result;
	}

private:
	virtual Pair<TVector<T, d>, bool> FindClosestIntersectionImp(const TVector<T, d>& StartPoint, const TVector<T, d>& EndPoint, const T Thickness) const override
	{
		check(MObjects.Num());
		auto ClosestIntersection = MObjects[0]->FindClosestIntersection(StartPoint, EndPoint, Thickness);
		T Length = ClosestIntersection.Second ? (ClosestIntersection.First - StartPoint).Size() : 0;
		for (int32 i = 1; i < MObjects.Num(); ++i)
		{
			auto NextClosestIntersection = MObjects[i]->FindClosestIntersection(StartPoint, EndPoint, Thickness);
			if (!NextClosestIntersection.Second)
				continue;
			T NewLength = (NextClosestIntersection.First - StartPoint).Size();
			if (!ClosestIntersection.Second || NewLength < Length)
			{
				Length = NewLength;
				ClosestIntersection = NextClosestIntersection;
			}
		}
		return ClosestIntersection;
	}

  private:
	TArray<TUniquePtr<FImplicitObject>> MObjects;
	TGeometryParticles<T, d> GeomParticles;
	TBoundingVolumeHierarchy<TGeometryParticles<T, d>, TArray<int32>, T, d>* Hierarchy;
	TAABB<T, d> MLocalBoundingBox;
	bool bHierarchyBuilt;
	TArray<int32> MOriginalParticleLookupHack;	//temp hack for finding original particles


	//needed for serialization
	TImplicitObjectUnion();
	friend FImplicitObject;	//needed for serialization
public:
	TMap<const FImplicitObject*, int32> MCollisionParticleLookupHack;	//temp hack for finding collision particles
};
}
