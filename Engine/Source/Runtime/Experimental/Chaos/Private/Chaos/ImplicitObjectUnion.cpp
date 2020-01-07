// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/BoundingVolumeHierarchy.h"

using namespace Chaos;

FImplicitObjectUnion::FImplicitObjectUnion(TArray<TUniquePtr<FImplicitObject>>&& Objects, const TArray<int32>& OriginalParticleLookupHack)
	: FImplicitObject(EImplicitObject::HasBoundingBox, ImplicitObjectType::Union)
	, MObjects(MoveTemp(Objects))
	, Hierarchy(new TBoundingVolumeHierarchy<TGeometryParticles<FReal,3>, TArray<int32>, FReal, 3>(GeomParticles))
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
			if (const TImplicitObjectTransformed<FReal,3>* Transformed = MObjects[i]->template GetObject<const TImplicitObjectTransformed<FReal,3>>())
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

FImplicitObjectUnion::FImplicitObjectUnion(FImplicitObjectUnion&& Other)
	: FImplicitObject(EImplicitObject::HasBoundingBox)
	, MObjects(MoveTemp(Other.MObjects))
	, GeomParticles(MoveTemp(Other.GeomParticles))
	, Hierarchy(new TBoundingVolumeHierarchy<TGeometryParticles<FReal,3>, TArray<int32>, FReal, 3>(MoveTemp(*Other.Hierarchy)))
	, MLocalBoundingBox(MoveTemp(Other.MLocalBoundingBox))
	, bHierarchyBuilt(Other.bHierarchyBuilt)
	, MOriginalParticleLookupHack(MoveTemp(Other.MOriginalParticleLookupHack))
	, MCollisionParticleLookupHack(MoveTemp(Other.MCollisionParticleLookupHack))
{
}

FImplicitObjectUnion::~FImplicitObjectUnion()
{
	delete Hierarchy;
}

void FImplicitObjectUnion::FindAllIntersectingObjects(TArray < Pair<const FImplicitObject*,FRigidTransform3>>& Out, const TAABB<FReal,3>& LocalBounds) const
{
	if (bHierarchyBuilt)
	{
		TArray<int32> Overlaps = Hierarchy->FindAllIntersections(LocalBounds);
		Out.Reserve(Out.Num() + Overlaps.Num());
		for (int32 Idx : Overlaps)
		{
			const FImplicitObject* Obj = GeomParticles.Geometry(Idx).Get();
			Out.Add(MakePair(Obj,FRigidTransform3(GeomParticles.X(Idx), GeomParticles.R(Idx))));
		}
	}
	else
	{
		for (const TUniquePtr<FImplicitObject>& Object : MObjects)
		{
			Object->FindAllIntersectingObjects(Out, LocalBounds);
		}
	}
}

TArray<int32> FImplicitObjectUnion::FindAllIntersectingChildren(const TAABB<FReal,3>& LocalBounds) const
{
	TArray<int32> IntersectingChildren;
	if (bHierarchyBuilt) //todo: make this work when hierarchy is not built
	{
		IntersectingChildren = Hierarchy->FindAllIntersections(LocalBounds);
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

TArray<int32> FImplicitObjectUnion::FindAllIntersectingChildren(const TSpatialRay<FReal,3>& LocalRay) const
{
	TArray<int32> IntersectingChildren;
	if (bHierarchyBuilt) //todo: make this work when hierarchy is not built
	{
		IntersectingChildren = Hierarchy->FindAllIntersections(LocalRay);
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

void FImplicitObjectUnion::CacheAllImplicitObjects()
{
	TArray < Pair<TSerializablePtr<FImplicitObject>,FRigidTransform3>> SubObjects;
	AccumulateAllSerializableImplicitObjectsHelper(SubObjects,FRigidTransform3::Identity);
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

			TBoundingVolumeHierarchy<TGeometryParticles<FReal,3>, TArray<int32>, FReal, 3> NewHierarchy(GeomParticles, 1);
			*Hierarchy = MoveTemp(NewHierarchy);
			bHierarchyBuilt = true;
		}
	}
}

void FImplicitObjectUnion::Serialize(FChaosArchive& Ar)
{
	FChaosArchiveScopedMemory ScopedMemory(Ar, GetTypeName(), false);
	FImplicitObject::SerializeImp(Ar);
	Ar << MObjects;
	TBox<FReal,3>::SerializeAsAABB(Ar, MLocalBoundingBox);
	Ar << GeomParticles << *Hierarchy << bHierarchyBuilt;
}

FImplicitObjectUnion::FImplicitObjectUnion() : FImplicitObject(EImplicitObject::HasBoundingBox, ImplicitObjectType::Union), Hierarchy(new TBoundingVolumeHierarchy<TGeometryParticles<FReal,3>, TArray<int32>, FReal, 3>(GeomParticles, 1)){}
