// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/BoundingVolumeHierarchy.h"

namespace Chaos
{
struct FLargeImplicitObjectUnionData
{
	TGeometryParticles<FReal,3> GeomParticles;
	TBoundingVolumeHierarchy<TGeometryParticles<FReal,3>, TArray<int32>, FReal, 3> Hierarchy;

	FLargeImplicitObjectUnionData(const TArray<Pair<TSerializablePtr<FImplicitObject>,FRigidTransform3>>& SubObjects)
	{
		const int32 NumObjects = SubObjects.Num();
		GeomParticles.Resize(NumObjects);
		for (int32 i = 0; i < NumObjects; ++i)
		{
			GeomParticles.X(i) = SubObjects[i].Second.GetLocation();
			GeomParticles.R(i) = SubObjects[i].Second.GetRotation();
			GeomParticles.SetGeometry(i, SubObjects[i].First);
			//check(!SubObjects[i].First->IsUnderlyingUnion());	//we don't support union of unions
		}

		Hierarchy = TBoundingVolumeHierarchy<TGeometryParticles<FReal,3>,TArray<int32>,FReal,3> (GeomParticles,1);
	}

	void Serialize(FChaosArchive& Ar)
	{
		Ar << GeomParticles << Hierarchy;
	}

	FLargeImplicitObjectUnionData(){}

	FLargeImplicitObjectUnionData(const FLargeImplicitObjectUnionData& Other) = delete;
	FLargeImplicitObjectUnionData& operator=(const FLargeImplicitObjectUnionData& Other) = delete;
};

FChaosArchive& operator<<(FChaosArchive& Ar, FLargeImplicitObjectUnionData& LargeUnionData)
{
	LargeUnionData.Serialize(Ar);
	return Ar;
}

FImplicitObjectUnion::FImplicitObjectUnion(TArray<TUniquePtr<FImplicitObject>>&& Objects)
	: FImplicitObject(EImplicitObject::HasBoundingBox, ImplicitObjectType::Union)
	, MObjects(MoveTemp(Objects))
	, MLocalBoundingBox()
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
	}

	CacheAllImplicitObjects();
}

FImplicitObjectUnion::FImplicitObjectUnion(FImplicitObjectUnion&& Other)
	: FImplicitObject(EImplicitObject::HasBoundingBox, ImplicitObjectType::Union)
	, MObjects(MoveTemp(Other.MObjects))
	, MLocalBoundingBox(MoveTemp(Other.MLocalBoundingBox))
	, LargeUnionData(MoveTemp(Other.LargeUnionData))
{
}

FImplicitObjectUnion::~FImplicitObjectUnion() = default;

void FImplicitObjectUnion::FindAllIntersectingObjects(TArray < Pair<const FImplicitObject*,FRigidTransform3>>& Out, const TAABB<FReal,3>& LocalBounds) const
{
	if (LargeUnionData)
	{
		TArray<int32> Overlaps = LargeUnionData->Hierarchy.FindAllIntersections(LocalBounds);
		Out.Reserve(Out.Num() + Overlaps.Num());
		for (int32 Idx : Overlaps)
		{
			const FImplicitObject* Obj = LargeUnionData->GeomParticles.Geometry(Idx).Get();
			Out.Add(MakePair(Obj,FRigidTransform3(LargeUnionData->GeomParticles.X(Idx), LargeUnionData->GeomParticles.R(Idx))));
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

TArray<int32> FImplicitObjectUnionClustered::FindAllIntersectingChildren(const TAABB<FReal,3>& LocalBounds) const
{
	TArray<int32> IntersectingChildren;
	if (LargeUnionData) //todo: make this work when hierarchy is not built
	{
		IntersectingChildren = LargeUnionData->Hierarchy.FindAllIntersections(LocalBounds);
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

TArray<int32> FImplicitObjectUnionClustered::FindAllIntersectingChildren(const TSpatialRay<FReal,3>& LocalRay) const
{
	TArray<int32> IntersectingChildren;
	if (LargeUnionData) //todo: make this work when hierarchy is not built
	{
		IntersectingChildren = LargeUnionData->Hierarchy.FindAllIntersections(LocalRay);
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
		constexpr int32 MinSubObjectsToCache = 32;	//todo(make this tunable?)
		if (NumObjects > MinSubObjectsToCache)
		{
			LargeUnionData = MakeUnique<FLargeImplicitObjectUnionData>(SubObjects);
		}
	}
}

void FImplicitObjectUnion::Serialize(FChaosArchive& Ar)
{
	Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);
	FChaosArchiveScopedMemory ScopedMemory(Ar, GetTypeName(), false);
	FImplicitObject::SerializeImp(Ar);
	Ar << MObjects;
	TBox<FReal,3>::SerializeAsAABB(Ar, MLocalBoundingBox);

	bool bHierarchyBuilt = LargeUnionData.Get() != nullptr;
	if(Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::UnionObjectsCanAvoidHierarchy)
	{
		LargeUnionData = MakeUnique<FLargeImplicitObjectUnionData>();
		Ar << *LargeUnionData;
		Ar << bHierarchyBuilt;
	}
	else
	{
		Ar << bHierarchyBuilt;
		if(bHierarchyBuilt)
		{
			if(Ar.IsLoading())
			{
				LargeUnionData = MakeUnique<FLargeImplicitObjectUnionData>();
			}
			Ar << *LargeUnionData;
		}
	}
}

FImplicitObjectUnion::FImplicitObjectUnion() : FImplicitObject(EImplicitObject::HasBoundingBox, ImplicitObjectType::Union){}


FImplicitObjectUnionClustered::FImplicitObjectUnionClustered(TArray<TUniquePtr<FImplicitObject>>&& Objects, const TArray<int32>& OriginalParticleLookupHack)
	: FImplicitObjectUnion(MoveTemp(Objects))
	, MOriginalParticleLookupHack(OriginalParticleLookupHack)
{
	Type = ImplicitObjectType::UnionClustered;
	for (int32 i = 0; i < MObjects.Num(); ++i)
	{
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
}

FImplicitObjectUnionClustered::FImplicitObjectUnionClustered(FImplicitObjectUnionClustered&& Other)
: FImplicitObjectUnion(MoveTemp(Other))
, MOriginalParticleLookupHack(MoveTemp(MOriginalParticleLookupHack))
, MCollisionParticleLookupHack(MoveTemp(MCollisionParticleLookupHack))
{
	Type = ImplicitObjectType::UnionClustered;
}

}
