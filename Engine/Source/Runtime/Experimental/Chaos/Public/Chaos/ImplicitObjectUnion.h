// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/ImplicitObject.h"

#include <memory>
#include "BoundingVolumeHierarchy.h"
#include "ImplicitObjectTransformed.h"
#include "ChaosArchive.h"

namespace Chaos
{
template<class T, int d>
class TImplicitObjectUnion : public TImplicitObject<T, d>
{
  public:

	using TImplicitObject<T, d>::GetTypeName;

	TImplicitObjectUnion(TArray<TUniquePtr<TImplicitObject<T, d>>>&& Objects, const TArray<int32>& OriginalParticleLookupHack = TArray<int32>())
	    : TImplicitObject<T, d>(EImplicitObject::HasBoundingBox, ImplicitObjectType::Union)
	    , MObjects(MoveTemp(Objects))
		, Hierarchy(GeomParticles)
	    , MLocalBoundingBox()
		, bHierarchyBuilt(false)
		, MOriginalParticleLookupHack(OriginalParticleLookupHack)
	{
		ensure(MObjects.Num());
		for (int32 i = 0; i < MObjects.Num(); ++i)
		{
			if (i > 0)
			{
				MLocalBoundingBox.GrowToInclude(MObjects[i]->BoundingBox());
			}
			else
			{
				MLocalBoundingBox = MObjects[i]->BoundingBox();
			}
			check(MOriginalParticleLookupHack.Num() == 0 || MOriginalParticleLookupHack.Num() == MObjects.Num());
			if (MOriginalParticleLookupHack.Num() > 0)
			{
				//this whole part sucks, only needed because of how we get union children. Need to refactor and enforce no unions of unions
				if (const TImplicitObjectTransformed<T, d>* Transformed = MObjects[i]->template GetObject<const TImplicitObjectTransformed<T, d>>())
				{
					MCollisionParticleLookupHack.Add(Transformed->GetTransformedObject(), MOriginalParticleLookupHack[i]);
				}
				else
				{
					ensure(false);	//shouldn't be here
				}
			}
		}

		CacheAllImplicitObjects();

	}
	TImplicitObjectUnion(const TImplicitObjectUnion<T, d>& Other) = delete;
	TImplicitObjectUnion(TImplicitObjectUnion<T, d>&& Other)
	    : TImplicitObject<T, d>(EImplicitObject::HasBoundingBox)
	    , MObjects(MoveTemp(Other.MObjects))
		, GeomParticles(MoveTemp(Other.GeomParticles))
		, Hierarchy(MoveTemp(Other.Hierarchy))
	    , MLocalBoundingBox(MoveTemp(Other.MLocalBoundingBox))
		, bHierarchyBuilt(Other.bHierarchyBuilt)
		, MOriginalParticleLookupHack(MoveTemp(Other.MOriginalParticleLookupHack))
		, MCollisionParticleLookupHack(MoveTemp(Other.MCollisionParticleLookupHack))
	{
	}
	virtual ~TImplicitObjectUnion() {}

	FORCEINLINE static ImplicitObjectType GetType()
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

	virtual const TBox<T, d>& BoundingBox() const override { return MLocalBoundingBox; }

	virtual void AccumulateAllImplicitObjects(TArray<Pair<const TImplicitObject<T, d>*, TRigidTransform<T, d>>>& Out, const TRigidTransform<T, d>& ParentTM) const
	{
		for (const TUniquePtr<TImplicitObject<T, d>>& Object : MObjects)
		{
			Object->AccumulateAllImplicitObjects(Out, ParentTM);
		}
	}

	virtual void AccumulateAllSerializableImplicitObjects(TArray<Pair<TSerializablePtr<TImplicitObject<T, d>>, TRigidTransform<T, d>>>& Out, const TRigidTransform<T, d>& ParentTM, TSerializablePtr<TImplicitObject<T,d>> This) const
	{
		AccumulateAllSerializableImplicitObjectsHelper(Out, ParentTM);
	}

	void AccumulateAllSerializableImplicitObjectsHelper(TArray<Pair<TSerializablePtr<TImplicitObject<T, d>>, TRigidTransform<T, d>>>& Out, const TRigidTransform<T, d>& ParentTM) const
	{
		for (const TUniquePtr<TImplicitObject<T, d>>& Object : MObjects)
		{
			Object->AccumulateAllSerializableImplicitObjects(Out, ParentTM, MakeSerializable(Object));
		}
	}

	virtual void FindAllIntersectingObjects(TArray < Pair<const TImplicitObject<T, d>*, TRigidTransform<T, d>>>& Out, const TBox<T, d>& LocalBounds) const
	{
		if (bHierarchyBuilt)
		{
			TArray<int32> Overlaps = Hierarchy.FindAllIntersections(LocalBounds);
			Out.Reserve(Out.Num() + Overlaps.Num());
			for (int32 Idx : Overlaps)
			{
				const TImplicitObject<T, d>* Obj = GeomParticles.Geometry(Idx).Get();
				Out.Add(MakePair(Obj, TRigidTransform<T, d>(GeomParticles.X(Idx), GeomParticles.R(Idx))));
			}
		}
		else
		{
			for (const TUniquePtr<TImplicitObject<T, d>>& Object : MObjects)
			{
				Object->FindAllIntersectingObjects(Out, LocalBounds);
			}
		}
	}

	TArray<int32> FindAllIntersectingChildren(const TBox<T, d>& LocalBounds) const
	{
		TArray<int32> IntersectingChildren;
		if (bHierarchyBuilt) //todo: make this work when hierarchy is not built
		{
			IntersectingChildren = Hierarchy.FindAllIntersections(LocalBounds);
			for (int32 i = IntersectingChildren.Num() - 1; i >= 0; --i)
			{
				const int32 Idx = IntersectingChildren[i];
				if (Idx < MOriginalParticleLookupHack.Num())
				{
					IntersectingChildren[i] = MOriginalParticleLookupHack[Idx];
				}
				else
				{
					IntersectingChildren.RemoveAtSwap(i);
				}
			}
			/*for (int32& Idx : IntersectingChildren)
			{
				Idx = MOriginalParticleLookupHack[Idx];
			}*/
		}
		else
		{
			IntersectingChildren = MOriginalParticleLookupHack;
		}

		return IntersectingChildren;
	}

	TArray<int32> FindAllIntersectingChildren(const TSpatialRay<T, d>& LocalRay) const
	{
		TArray<int32> IntersectingChildren;
		if (bHierarchyBuilt) //todo: make this work when hierarchy is not built
		{
			IntersectingChildren = Hierarchy.FindAllIntersections(LocalRay);
			for (int32 i = IntersectingChildren.Num() - 1; i >= 0; --i)
			{
				const int32 Idx = IntersectingChildren[i];
				if (Idx < MOriginalParticleLookupHack.Num())
				{
					IntersectingChildren[i] = MOriginalParticleLookupHack[Idx];
				}
				else
				{
					IntersectingChildren.RemoveAtSwap(i);
				}
			}
			/*for (int32& Idx : IntersectingChildren)
			{
				Idx = MOriginalParticleLookupHack[Idx];
			}*/
		}
		else
		{
			IntersectingChildren = MOriginalParticleLookupHack;
		}

		return IntersectingChildren;
	}

	virtual void CacheAllImplicitObjects()
	{
		TArray < Pair<TSerializablePtr<TImplicitObject<T, d>>, TRigidTransform<T, d>>> SubObjects;
		AccumulateAllSerializableImplicitObjectsHelper(SubObjects, TRigidTransform<T, d>::Identity);
		//build hierarchy
		{
			const int32 NumObjects = SubObjects.Num();
			constexpr int32 MinSubObjectsToCache = 8;	//todo(make this tunable?)
			if (NumObjects > MinSubObjectsToCache)
			{
				GeomParticles.Resize(NumObjects);
				for (int32 i = 0; i < NumObjects; ++i)
				{
					GeomParticles.X(i) = SubObjects[i].Second.GetLocation();
					GeomParticles.R(i) = SubObjects[i].Second.GetRotation();
					GeomParticles.SetGeometry(i, SubObjects[i].First);
					//check(!SubObjects[i].First->IsUnderlyingUnion());	//we don't support union of unions
				}

				TBoundingVolumeHierarchy<TGeometryParticles<T, d>, TArray<int32>, T, d> NewHierarchy(GeomParticles, 1);
				Hierarchy = MoveTemp(NewHierarchy);
				bHierarchyBuilt = true;
			}
		}
	}

	virtual bool Raycast(const TVector<T, d>& StartPoint, const TVector<T, d>& Dir, const T Length, const T Thickness, T& OutTime, TVector<T, d>& OutPosition, TVector<T, d>& OutNormal, int32& OutFaceIndex) const override
	{
		T MinTime = 0;	//initialization not needed, but doing it to avoid warning
		bool bFound = false;

		for (const TUniquePtr<TImplicitObject<T, d>>& Obj : MObjects)
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
		for (const TUniquePtr<TImplicitObject<T, d>>& Obj : MObjects)
		{
			if (Obj->Overlap(Point, Thickness))
			{
				return true;
			}
		}

		return false;
	}

	virtual void Serialize(FChaosArchive& Ar) override
	{
		FChaosArchiveScopedMemory ScopedMemory(Ar, GetTypeName(), false);
		TImplicitObject<T, d>::SerializeImp(Ar);
		Ar << MObjects << MLocalBoundingBox << GeomParticles << Hierarchy << bHierarchyBuilt;
	}

	virtual bool IsValidGeometry() const
	{
		bool bValid = TImplicitObject<T, d>::IsValidGeometry();
		bValid = bValid && MObjects.Num();
		return bValid;
	}

	const TArray<TUniquePtr<TImplicitObject<T, d>>>& GetObjects() const { return MObjects; }

	virtual uint32 GetTypeHash() const override
	{
		uint32 Result = 0;

		// Union hash is just the hash of all internal objects
		for(const TUniquePtr<TImplicitObject<T, d>>& InnerObj : MObjects)
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
	TArray<TUniquePtr<TImplicitObject<T, d>>> MObjects;
	TGeometryParticles<T, d> GeomParticles;
	TBoundingVolumeHierarchy<TGeometryParticles<T, d>, TArray<int32>, T, d> Hierarchy;
	TBox<T, d> MLocalBoundingBox;
	bool bHierarchyBuilt;
	TArray<int32> MOriginalParticleLookupHack;	//temp hack for finding original particles


	//needed for serialization
	TImplicitObjectUnion() : TImplicitObject<T, d>(EImplicitObject::HasBoundingBox, ImplicitObjectType::Union), Hierarchy(GeomParticles, 1){}
	friend TImplicitObject<T, d>;	//needed for serialization
public:
	TMap<const TImplicitObject<T,d>*, int32> MCollisionParticleLookupHack;	//temp hack for finding collision particles
};
}
