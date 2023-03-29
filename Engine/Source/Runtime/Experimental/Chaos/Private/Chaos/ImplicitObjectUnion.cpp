// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/ImplicitObjectBVH.h"
#include "Chaos/BoundingVolumeHierarchy.h"

#include "UObject/FortniteMainBranchObjectVersion.h"

//UE_DISABLE_OPTIMIZATION

namespace Chaos
{
	namespace CVars
	{
		int32 ChaosUnionBVHMinShapes = 32;
		int32 ChaosUnionBVHMaxDepth = 8;
		bool bChaosUnionBVHEnabled = true;
		FAutoConsoleVariableRef CVarChaosUnionBVHMinShapes(TEXT("p.Chaos.Collision.UnionBVH.NumShapes"), ChaosUnionBVHMinShapes, TEXT("If a geometry hierarchy has this many shapes, wrap it in a BVH for collision detection (negative to disable BVH)"));
		FAutoConsoleVariableRef CVarChaosUnionBVHMaxDepth(TEXT("p.Chaos.Collision.UnionBVH.MaxDepth"), ChaosUnionBVHMaxDepth, TEXT("The allowed depth of the BVH when used to wrap a shape hiererchy"));
		FAutoConsoleVariableRef CVarChaosUnionBVHEnabled(TEXT("p.Chaos.Collision.UnionBVH.Enabled"), bChaosUnionBVHEnabled, TEXT("Set to false to disable use of BVH during collision detection (without affecting creations and serialization)"));
	}

FImplicitObjectUnion::FImplicitObjectUnion() 
	: FImplicitObject(EImplicitObject::HasBoundingBox, ImplicitObjectType::Union)
	, MLocalBoundingBox()
	, NumLeafObjects(0)
	, Flags()
{
}

FImplicitObjectUnion::FImplicitObjectUnion(TArray<TUniquePtr<FImplicitObject>>&& Objects)
	: FImplicitObject(EImplicitObject::HasBoundingBox, ImplicitObjectType::Union)
	, MObjects(MoveTemp(Objects))
	, MLocalBoundingBox()
	, NumLeafObjects(0)
	, Flags()
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

	SetNumLeafObjects(Private::FImplicitBVH::CountLeafObjects(MakeArrayView(MObjects)));
}

FImplicitObjectUnion::FImplicitObjectUnion(FImplicitObjectUnion&& Other)
	: FImplicitObject(EImplicitObject::HasBoundingBox, ImplicitObjectType::Union)
	, MObjects(MoveTemp(Other.MObjects))
	, MLocalBoundingBox(MoveTemp(Other.MLocalBoundingBox))
	, BVH(MoveTemp(Other.BVH))
	, NumLeafObjects(Other.NumLeafObjects)
{
	Flags.Bits = Other.Flags.Bits;
}

FImplicitObjectUnion::~FImplicitObjectUnion() = default;

void FImplicitObjectUnion::Combine(TArray<TUniquePtr<FImplicitObject>>& OtherObjects)
{
	ensure(MObjects.Num());

	for (int32 i = 0; i < OtherObjects.Num(); ++i)
	{
		MLocalBoundingBox.GrowToInclude(OtherObjects[i]->BoundingBox());
	}

	MObjects.Reserve(MObjects.Num() + OtherObjects.Num());
	for (TUniquePtr<FImplicitObject>& ChildObject : OtherObjects)
	{
		SetNumLeafObjects(GetNumLeafObjects() + Private::FImplicitBVH::CountLeafObjects(MakeArrayView(&ChildObject, 1)));

		MObjects.Add(MoveTemp(ChildObject));
	}

	RebuildBVH();
}

void FImplicitObjectUnion::RemoveAt(int32 RemoveIndex)
{
	if (RemoveIndex < MObjects.Num())
	{
		SetNumLeafObjects(GetNumLeafObjects() - Private::FImplicitBVH::CountLeafObjects(MakeArrayView(&MObjects[RemoveIndex], 1)));

		MObjects[RemoveIndex].Reset(nullptr);
		MObjects.RemoveAt(RemoveIndex);
	}

	MLocalBoundingBox = FAABB3::EmptyAABB();
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

	RebuildBVH();
}

void FImplicitObjectUnion::SetNumLeafObjects(int32 InNumLeafObjects)
{
	constexpr int32 MaxNumLeafObjects = int32(TNumericLimits<decltype(NumLeafObjects)>::Max());
	ensure(InNumLeafObjects <= MaxNumLeafObjects);
	check(InNumLeafObjects >= 0);

	NumLeafObjects = uint16(FMath::Min(InNumLeafObjects, MaxNumLeafObjects));
}

void FImplicitObjectUnion::CreateBVH()
{
	if (Flags.bAllowBVH)
	{
		const int32 MinBVHShapes = CVars::ChaosUnionBVHMinShapes;
		const int32 MaxBVHDepth = CVars::ChaosUnionBVHMaxDepth;
		BVH = Private::FImplicitBVH::TryMake(MakeArrayView(MObjects), MinBVHShapes, MaxBVHDepth);
		Flags.bHasBVH = BVH.IsValid();
	}
}

void FImplicitObjectUnion::DestroyBVH()
{
	if (BVH.IsValid())
	{
		BVH.Reset();
		Flags.bHasBVH = false;
	}
}

void FImplicitObjectUnion::RebuildBVH()
{
	DestroyBVH();
	CreateBVH();
}

void FImplicitObjectUnion::FindAllIntersectingObjects(TArray<Pair<const FImplicitObject*,FRigidTransform3>>& Out, const FAABB3& LocalBounds) const
{
	if (BVH.IsValid() && CVars::bChaosUnionBVHEnabled)
	{
		TArray<int32> Overlaps = BVH->GetBVH().FindAllIntersections(LocalBounds);
		Out.Reserve(Out.Num() + Overlaps.Num());
		for (int32 Idx : Overlaps)
		{
			const FImplicitObject* Obj = BVH->GetGeometry(Idx);
			Out.Add(MakePair(Obj, BVH->GetTransform(Idx)));
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

void FImplicitObjectUnion::VisitOverlappingLeafObjectsImpl(
	const FAABB3& LocalBounds,
	const FRigidTransform3& ObjectTransform,
	const int32 InRootObjectIndex,
	int32& ObjectIndex,
	int32& LeafObjectIndex,
	const FImplicitHierarchyVisitor& VisitorFunc) const
{
	if (BVH.IsValid() && CVars::bChaosUnionBVHEnabled)
	{
		// Visit children
		BVH->VisitAllIntersections(LocalBounds,
			[this, &ObjectIndex, &LeafObjectIndex, &ObjectTransform, &VisitorFunc](const int32 BVHObjectIndex)
			{
				VisitorFunc(BVH->GetGeometry(BVHObjectIndex), BVH->GetTransform(BVHObjectIndex) * ObjectTransform, BVH->GetRootObjectIndex(BVHObjectIndex), BVH->GetObjectIndex(BVHObjectIndex), BVHObjectIndex);
			});
	}
	else
	{
		// Skip self
		++ObjectIndex;

		for (int32 BVHObjectIndex = 0; BVHObjectIndex < MObjects.Num(); ++BVHObjectIndex)
		{
			// If we are the root our object index is the root index, otherwise just pass on the value we were given (from the actual root)
			const int32 RootObjectIndex = (InRootObjectIndex != INDEX_NONE) ? InRootObjectIndex : BVHObjectIndex;

			MObjects[BVHObjectIndex]->VisitOverlappingLeafObjectsImpl(LocalBounds, ObjectTransform, RootObjectIndex, ObjectIndex, LeafObjectIndex, VisitorFunc);
		}
	}
}

void FImplicitObjectUnion::VisitLeafObjectsImpl(
	const FRigidTransform3& ObjectTransform,
	const int32 InRootObjectIndex,
	int32& ObjectIndex,
	int32& LeafObjectIndex,
	const FImplicitHierarchyVisitor& VisitorFunc) const
{
	// Skip self
	++ObjectIndex;

	for (int32 BVHObjectIndex = 0; BVHObjectIndex < MObjects.Num(); ++BVHObjectIndex)
	{
		// If we are the root our object index is the root index, otherwise just pass on the value we were given (from the actual root)
		const int32 RootObjectIndex = (InRootObjectIndex != INDEX_NONE) ? InRootObjectIndex : BVHObjectIndex;

		MObjects[BVHObjectIndex]->VisitLeafObjectsImpl(ObjectTransform, RootObjectIndex, ObjectIndex, LeafObjectIndex, VisitorFunc);
	}
}

void FImplicitObjectUnion::VisitObjectsImpl(
	const FRigidTransform3& ObjectTransform,
	const int32 InRootObjectIndex,
	int32& ObjectIndex,
	int32& LeafObjectIndex,
	const FImplicitHierarchyVisitor& VisitorFunc) const
{
	// Visit self
	VisitorFunc(this, ObjectTransform, InRootObjectIndex, ObjectIndex, INDEX_NONE);
	++ObjectIndex;

	// Visit Children
	for (int32 BVHObjectIndex = 0; BVHObjectIndex < MObjects.Num(); ++BVHObjectIndex)
	{
		// If we are the root our object index is the root index, otherwise just pass on the value we were given (from the actual root)
		const int32 RootObjectIndex = (InRootObjectIndex != INDEX_NONE) ? InRootObjectIndex : BVHObjectIndex;

		MObjects[BVHObjectIndex]->VisitObjectsImpl(ObjectTransform, RootObjectIndex, ObjectIndex, LeafObjectIndex, VisitorFunc);
	}
}

TUniquePtr<FImplicitObject> FImplicitObjectUnion::Copy() const
{
	TArray<TUniquePtr<FImplicitObject>> CopyOfObjects;
	CopyOfObjects.Reserve(MObjects.Num());
	for (const TUniquePtr<FImplicitObject>& Object : MObjects)
	{
		CopyOfObjects.Emplace(Object->Copy());
	}
	return MakeUnique<FImplicitObjectUnion>(MoveTemp(CopyOfObjects));
}

TUniquePtr<FImplicitObject> FImplicitObjectUnion::CopyWithScale(const FVec3& Scale) const
{
	TArray<TUniquePtr<FImplicitObject>> CopyOfObjects;
	CopyOfObjects.Reserve(MObjects.Num());
	for (const TUniquePtr<FImplicitObject>& Object : MObjects)
	{
		CopyOfObjects.Emplace(Object->CopyWithScale(Scale));
	}
	return MakeUnique<FImplicitObjectUnion>(MoveTemp(CopyOfObjects));
}

TUniquePtr<FImplicitObject> FImplicitObjectUnion::DeepCopy() const
{
	TArray<TUniquePtr<FImplicitObject>> CopyOfObjects;
	CopyOfObjects.Reserve(MObjects.Num());
	for (const TUniquePtr<FImplicitObject>& Object : MObjects)
	{
		CopyOfObjects.Emplace(Object->DeepCopy());
	}
	return MakeUnique<FImplicitObjectUnion>(MoveTemp(CopyOfObjects));
}

TUniquePtr<FImplicitObject> FImplicitObjectUnion::DeepCopyWithScale(const FVec3& Scale) const
{
	TArray<TUniquePtr<FImplicitObject>> CopyOfObjects;
	CopyOfObjects.Reserve(MObjects.Num());
	for (const TUniquePtr<FImplicitObject>& Object : MObjects)
	{
		CopyOfObjects.Emplace(Object->DeepCopyWithScale(Scale));
	}
	return MakeUnique<FImplicitObjectUnion>(MoveTemp(CopyOfObjects));
}

void FImplicitObjectUnion::ForEachObject(TFunctionRef<bool(const FImplicitObject&, const FRigidTransform3&)> Lambda) const
{
	// @todo(chaos): this implementation is strange. If we have as BVH we will visit all children in the hierarchy, but if not
	// we only visit our immediate children, and not their children. It should probably just ignore the BVH?
	if (BVH.IsValid())
	{
		for (int32 Index = 0; Index < BVH->NumObjects(); ++Index)
		{
			if (const FImplicitObject* SubObject = BVH->GetGeometry(Index))
			{
				if (Lambda(*SubObject, BVH->GetTransform(Index)))
				{
					break;
				}
			}
		}
	}
	else
	{
		for (const TUniquePtr<FImplicitObject>& Object : MObjects)
		{
			if (Object)
			{
				if (Lambda(*Object, FRigidTransform3::Identity))
				{
					break;
				}
			}
		}
	}
}

void FImplicitObjectUnion::Serialize(FChaosArchive& Ar)
{
	Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);
	Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);

	FChaosArchiveScopedMemory ScopedMemory(Ar, GetTypeName(), false);
	FImplicitObject::SerializeImp(Ar);
	Ar << MObjects;
	TBox<FReal, 3>::SerializeAsAABB(Ar, MLocalBoundingBox);

	bool bHierarchyBuilt = BVH.IsValid();
	if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::UnionObjectsCanAvoidHierarchy)
	{
		LegacySerializeBVH(Ar);
		Ar << bHierarchyBuilt;
	}
	else if (Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::ChaosImplicitObjectUnionBVHRefactor)
	{
		Ar << bHierarchyBuilt;
		if (bHierarchyBuilt)
		{
			LegacySerializeBVH(Ar);
		}
	}
	else
	{
		Ar << Flags.Bits;
		Ar << NumLeafObjects;
		if (Flags.bHasBVH)
		{
			if (Ar.IsLoading())
			{
				BVH = Private::FImplicitBVH::MakeEmpty();
			}
			Ar << *BVH;
		}
	}
}

void FImplicitObjectUnion::LegacySerializeBVH(FChaosArchive& Ar)
{
	// We should only ever be loading old data. never saving it
	check(Ar.IsLoading());

	// The old data structure used FGeometryParticles which contains a lot of data we don't need
	struct FLargeImplicitObjectUnionData
	{
		FGeometryParticles GeomParticles;
		TBoundingVolumeHierarchy<FGeometryParticles, TArray<int32>> Hierarchy;
	};

	// Load the old data structure and chuck it away
	{
		FLargeImplicitObjectUnionData LegacyData;
		Ar << LegacyData.GeomParticles << LegacyData.Hierarchy;
	}

	// Count the objects in the hierarchy
	SetNumLeafObjects(Private::FImplicitBVH::CountLeafObjects(MakeArrayView(MObjects)));

	// Only the root Union should allow BVH, but we don't know which that is at this stage
	// so just revert to the original behaviour of every Union potentially having a BVH
	Flags.bAllowBVH = true;
	RebuildBVH();
}

FImplicitObjectUnionClustered::FImplicitObjectUnionClustered()
	: FImplicitObjectUnion()
{
	Type = ImplicitObjectType::UnionClustered;
}

FImplicitObjectUnionClustered::FImplicitObjectUnionClustered(
	TArray<TUniquePtr<FImplicitObject>>&& Objects, 
	const TArray<FPBDRigidParticleHandle*>& OriginalParticleLookupHack)
    : FImplicitObjectUnion(MoveTemp(Objects))
	, MOriginalParticleLookupHack(OriginalParticleLookupHack)
{
	Type = ImplicitObjectType::UnionClustered;
	check(MOriginalParticleLookupHack.Num() == 0 || MOriginalParticleLookupHack.Num() == MObjects.Num());
	MCollisionParticleLookupHack.Reserve(FMath::Min(MOriginalParticleLookupHack.Num(), MObjects.Num()));
	for (int32 i = 0; MOriginalParticleLookupHack.Num() > 0 && i < MObjects.Num(); ++i)
	{
		// This whole part sucks, only needed because of how we get union 
		// children. Need to refactor and enforce no unions of unions.
		if (const TImplicitObjectTransformed<FReal, 3>* Transformed = 
			MObjects[i]->template GetObject<const TImplicitObjectTransformed<FReal, 3>>())
		{
			// Map const TImplicitObject<T,d>* to int32, where the latter
			// was the RigidBodyId
			MCollisionParticleLookupHack.Add(
				Transformed->GetTransformedObject(), MOriginalParticleLookupHack[i]);
		}
		else
		{
			ensure(false);	//shouldn't be here
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

void FImplicitObjectUnionClustered::FindAllIntersectingClusteredObjects(TArray<FLargeUnionClusteredImplicitInfo>& Out, const FAABB3& LocalBounds) const
{
	if (BVH.IsValid())
	{
		TArray<int32> Overlaps = BVH->GetBVH().FindAllIntersections(LocalBounds);
		Out.Reserve(Out.Num() + Overlaps.Num());
		for (int32 Idx : Overlaps)
		{
			const FImplicitObject* Obj = BVH->GetGeometry(Idx);
			const FBVHParticles* Simplicial = MOriginalParticleLookupHack.IsValidIndex(Idx) ? MOriginalParticleLookupHack[Idx]->CollisionParticles().Get() : nullptr;
			Out.Add(FLargeUnionClusteredImplicitInfo(Obj, BVH->GetTransform(Idx), Simplicial));
		}
	}
	else
	{
		TArray<Pair<const FImplicitObject*, FRigidTransform3>> LocalOut;
		TArray<int32> Idxs;
		for (int32 Idx = 0; Idx < MObjects.Num(); ++Idx)
		{
			int32 NumOut = LocalOut.Num();
			const TUniquePtr<FImplicitObject>& Object = MObjects[Idx];
			Object->FindAllIntersectingObjects(LocalOut, LocalBounds);
			for (int32 i = NumOut; i < LocalOut.Num(); ++i)
			{
				Idxs.Add(Idx);
			}
		}
		for (int32 Idx = 0; Idx < LocalOut.Num(); ++Idx)
		{
			auto& OutElem = LocalOut[Idx];
			const FBVHParticles* Simplicial = MOriginalParticleLookupHack.IsValidIndex(Idxs[Idx]) ? MOriginalParticleLookupHack[Idxs[Idx]]->CollisionParticles().Get() : nullptr;

			Out.Add(FLargeUnionClusteredImplicitInfo(OutElem.First, OutElem.Second, Simplicial));
		}
	}
}

TArray<FPBDRigidParticleHandle*>
FImplicitObjectUnionClustered::FindAllIntersectingChildren(const FAABB3& LocalBounds) const
{
	TArray<FPBDRigidParticleHandle*> IntersectingChildren;
	if (BVH.IsValid()) //todo: make this work when hierarchy is not built
	{
		TArray<int32> IntersectingIndices = BVH->GetBVH().FindAllIntersections(LocalBounds);
		IntersectingChildren.Reserve(IntersectingIndices.Num());
		for (const int32 Idx : IntersectingIndices)
		{
			if (MOriginalParticleLookupHack.IsValidIndex(Idx))
			{
				IntersectingChildren.Add(MOriginalParticleLookupHack[Idx]);
			}
		}
	}
	else
	{
		IntersectingChildren = MOriginalParticleLookupHack;
	}
	return IntersectingChildren;
}


const FPBDRigidParticleHandle* FImplicitObjectUnionClustered::FindParticleForImplicitObject(const FImplicitObject* Object) const
{
	typedef FPBDRigidParticleHandle* ValueType;

	const TImplicitObjectTransformed<FReal, 3>* AsTransformed = Object->template GetObject<TImplicitObjectTransformed<FReal, 3>>();
	if(AsTransformed)
	{
		const ValueType* Handle = MCollisionParticleLookupHack.Find(AsTransformed->GetTransformedObject());
		return Handle ? *Handle : nullptr;
	}

	const ValueType* Handle = MCollisionParticleLookupHack.Find(Object);
	return Handle ? *Handle : nullptr;
}

const FBVHParticles* FImplicitObjectUnionClustered::GetChildSimplicial(const int32 ChildIndex) const
{
	if (MOriginalParticleLookupHack.IsValidIndex(ChildIndex))
	{
		return MOriginalParticleLookupHack[ChildIndex]->CollisionParticles().Get();
	}
	return nullptr;
}

} // namespace Chaos
