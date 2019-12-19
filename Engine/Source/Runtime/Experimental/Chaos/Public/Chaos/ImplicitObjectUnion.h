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

struct FLargeImplicitObjectUnionData;

class CHAOS_API FImplicitObjectUnion : public FImplicitObject
{
  public:

	using FImplicitObject::GetTypeName;

	FImplicitObjectUnion(TArray<TUniquePtr<FImplicitObject>>&& Objects);
	FImplicitObjectUnion(const FImplicitObjectUnion& Other) = delete;
	FImplicitObjectUnion(FImplicitObjectUnion&& Other);
	virtual ~FImplicitObjectUnion();

	FORCEINLINE static constexpr EImplicitObjectType StaticType()
	{
		return ImplicitObjectType::Union;
	}

	virtual FReal PhiWithNormal(const FVec3& x, FVec3& Normal) const override
	{
		FReal Phi = TNumericLimits<FReal>::Max();
		bool NeedsNormalize = false;
		for (int32 i = 0; i < MObjects.Num(); ++i)
		{
			if(!ensure(MObjects[i]))
			{
				continue;
			}
			FVec3 NextNormal;
			FReal NextPhi = MObjects[i]->PhiWithNormal(x, NextNormal);
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

	virtual const TAABB<FReal, 3>& BoundingBox() const override { return MLocalBoundingBox; }

	virtual void AccumulateAllImplicitObjects(TArray<Pair<const FImplicitObject*, FRigidTransform3>>& Out, const FRigidTransform3& ParentTM) const
	{
		for (const TUniquePtr<FImplicitObject>& Object : MObjects)
		{
			Object->AccumulateAllImplicitObjects(Out, ParentTM);
		}
	}

	virtual void AccumulateAllSerializableImplicitObjects(TArray<Pair<TSerializablePtr<FImplicitObject>, FRigidTransform3>>& Out, const FRigidTransform3& ParentTM, TSerializablePtr<FImplicitObject> This) const
	{
		AccumulateAllSerializableImplicitObjectsHelper(Out, ParentTM);
	}

	void AccumulateAllSerializableImplicitObjectsHelper(TArray<Pair<TSerializablePtr<FImplicitObject>, FRigidTransform3>>& Out, const FRigidTransform3& ParentTM) const
	{
		for (const TUniquePtr<FImplicitObject>& Object : MObjects)
		{
			Object->AccumulateAllSerializableImplicitObjects(Out, ParentTM, MakeSerializable(Object));
		}
	}

	virtual void FindAllIntersectingObjects(TArray < Pair<const FImplicitObject*, FRigidTransform3>>& Out, const TAABB<FReal,3>& LocalBounds) const;
	virtual void CacheAllImplicitObjects();

	virtual bool Raycast(const FVec3& StartPoint, const FVec3& Dir, const FReal Length, const FReal Thickness, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex) const override
	{
		FReal MinTime = 0;	//initialization not needed, but doing it to avoid warning
		bool bFound = false;

		for (const TUniquePtr<FImplicitObject>& Obj : MObjects)
		{
			FVec3 Position;
			FVec3 Normal;
			FReal Time;
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

	virtual bool Overlap(const FVec3& Point, const FReal Thickness) const override
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

protected:
	virtual Pair<FVec3, bool> FindClosestIntersectionImp(const FVec3& StartPoint, const FVec3& EndPoint, const FReal Thickness) const override
	{
		check(MObjects.Num());
		auto ClosestIntersection = MObjects[0]->FindClosestIntersection(StartPoint, EndPoint, Thickness);
		FReal Length = ClosestIntersection.Second ? (ClosestIntersection.First - StartPoint).Size() : 0;
		for (int32 i = 1; i < MObjects.Num(); ++i)
		{
			auto NextClosestIntersection = MObjects[i]->FindClosestIntersection(StartPoint, EndPoint, Thickness);
			if (!NextClosestIntersection.Second)
				continue;
			FReal NewLength = (NextClosestIntersection.First - StartPoint).Size();
			if (!ClosestIntersection.Second || NewLength < Length)
			{
				Length = NewLength;
				ClosestIntersection = NextClosestIntersection;
			}
		}
		return ClosestIntersection;
	}

  protected:
	TArray<TUniquePtr<FImplicitObject>> MObjects;
	TAABB<FReal,3> MLocalBoundingBox;
	TUniquePtr<FLargeImplicitObjectUnionData> LargeUnionData;	//only needed when there are many objects

	//needed for serialization
	FImplicitObjectUnion();
	friend FImplicitObject;	//needed for serialization
public:
};

class CHAOS_API FImplicitObjectUnionClustered: public FImplicitObjectUnion
{
public:

	FImplicitObjectUnionClustered(TArray<TUniquePtr<FImplicitObject>>&& Objects,const TArray<int32>& OriginalParticleLookupHack = TArray<int32>());
	FImplicitObjectUnionClustered()
		: FImplicitObjectUnion()
	{
		Type = ImplicitObjectType::UnionClustered;
	}
	FImplicitObjectUnionClustered(const FImplicitObjectUnionClustered& Other) = delete;
	FImplicitObjectUnionClustered(FImplicitObjectUnionClustered&& Other);
	virtual ~FImplicitObjectUnionClustered() = default;

	FORCEINLINE static constexpr EImplicitObjectType StaticType()
	{
		return ImplicitObjectType::UnionClustered;
	}

	TArray<int32> FindAllIntersectingChildren(const TAABB<FReal,3>& LocalBounds) const;
	TArray<int32> FindAllIntersectingChildren(const TSpatialRay<FReal,3>& LocalRay) const;

private:
	TArray<int32> MOriginalParticleLookupHack;	//temp hack for finding original particles
	TMap<const FImplicitObject*,int32> MCollisionParticleLookupHack;	//temp hack for finding collision particles
};
}
