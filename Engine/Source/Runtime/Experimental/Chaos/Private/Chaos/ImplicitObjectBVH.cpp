// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/ImplicitObjectBVH.h"
#include "Chaos/ChaosArchive.h"

//UE_DISABLE_OPTIMIZATION

namespace Chaos
{
	namespace CVars
	{
		extern int32 ChaosUnionBVHMaxDepth;
	}

	namespace Private
	{

		FImplicitBVHObject::FImplicitBVHObject()
		{
		}

		FImplicitBVHObject::FImplicitBVHObject(
			const TSerializablePtr<FImplicitObject>& InGeometry, 
			const FVec3& InX, 
			const FRotation3& InR, 
			const FAABB3& InBounds,
			const int32 InRootObjectIndex,
			const int32 InObjectIndex)
			: R(FRotation3f(InR))
			, X(FVec3f(InX))
			, Bounds(FAABB3f(InBounds))
			, Geometry(InGeometry)
			, RootObjectIndex(InRootObjectIndex)
			, ObjectIndex(InObjectIndex)
		{
		}

		///////////////////////////////////////////////////////////////////////////////////////////
		///////////////////////////////////////////////////////////////////////////////////////////

		int32 FImplicitBVH::CountLeafObjects(const TArrayView<const TUniquePtr<FImplicitObject>>& InRootObjects)
		{
			// Count the objects in the hierarchy
			// @todo(chaos): provide a visitor that does not check bounds
			int32 NumObjects = 0;
			for (const TUniquePtr<FImplicitObject>& RootObject : InRootObjects)
			{
				RootObject->VisitLeafObjects(
					[&NumObjects](const FImplicitObject* Object, const FRigidTransform3& ParentTransform, const int32 RootObjectIndex, const int32 ObjectIndex, const int32 LeafObjectIndex)
					{ 
						++NumObjects;
					});
			}
			return NumObjects;
		}

		FImplicitBVH::FObjects FImplicitBVH::CollectLeafObjects(const TArrayView<const TUniquePtr<FImplicitObject>>& InRootObjects)
		{
			// We visit the hierarchy once to ensure we can create a tight-fitting array of leaf objects (the array growth
			// policy will over-allocate if we don't size exactly)
			TArray<FImplicitBVHObject> Objects;
			Objects.Reserve(CountLeafObjects(InRootObjects));

			for (int32 RootObjectIndex = 0; RootObjectIndex < InRootObjects.Num(); ++RootObjectIndex)
			{
				const TUniquePtr<FImplicitObject>& RootObject = InRootObjects[RootObjectIndex];

				RootObject->VisitLeafObjects(
					[RootObjectIndex, &Objects](const FImplicitObject* Object, const FRigidTransform3& ParentTransform, const int32 UnusedRootObjectIndex, const int32 ObjectIndex, const int32 UnusedLeafObjectIndex)
					{
						// @todo(chaos): clean this up (SetFromRawLowLevel). We know all the objects we visit are children of a UniquePtr because we own it
						TSerializablePtr<FImplicitObject> SerializableObject;
						SerializableObject.SetFromRawLowLevel(Object);

						Objects.Emplace(
							SerializableObject,
							ParentTransform.GetTranslation(),
							ParentTransform.GetRotation(),
							Object->CalculateTransformedBounds(ParentTransform),
							RootObjectIndex,
							ObjectIndex);
					});
			}

			return Objects;
		}

		TUniquePtr<FImplicitBVH> FImplicitBVH::MakeEmpty()
		{
			return TUniquePtr<FImplicitBVH>(new FImplicitBVH());
		}

		TUniquePtr<FImplicitBVH> FImplicitBVH::TryMake(const TArrayView<const TUniquePtr<FImplicitObject>>& InRootObjects, const int32 InMinObjects, const int32 InMaxBVHDepth)
		{
			TArray<FImplicitBVHObject> Objects = CollectLeafObjects(InRootObjects);
			if (Objects.Num() > InMinObjects)
			{
				return TUniquePtr<FImplicitBVH>(new FImplicitBVH(MoveTemp(Objects), InMaxBVHDepth));
			}
			return TUniquePtr<FImplicitBVH>();
		}

		///////////////////////////////////////////////////////////////////////////////////////////
		///////////////////////////////////////////////////////////////////////////////////////////

		FImplicitBVH::FImplicitBVH()
		{
		}

		FImplicitBVH::FImplicitBVH(FObjects&& InObjects, const int32 InMaxBVHDepth)
		{
			Init(MoveTemp(InObjects), InMaxBVHDepth);
		}

		FImplicitBVH::~FImplicitBVH()
		{
		}

		void FImplicitBVH::Init(FObjects&& InObjects, const int32 InMaxBVHDepth)
		{
			Objects = MoveTemp(InObjects);
			BVH = FBVH(Objects, FMath::Max(1, InMaxBVHDepth));
		}

		///////////////////////////////////////////////////////////////////////////////////////////
		///////////////////////////////////////////////////////////////////////////////////////////
		// 
		// SERIALIZATION
		// 
		///////////////////////////////////////////////////////////////////////////////////////////
		///////////////////////////////////////////////////////////////////////////////////////////

		FChaosArchive& operator<<(FChaosArchive& Ar, FImplicitBVH& BVH)
		{ 
			return BVH.Serialize(Ar);
		}

		FChaosArchive& operator<<(FChaosArchive& Ar, FImplicitBVHObject& BVHObject)
		{ 
			return BVHObject.Serialize(Ar);
		}

		FChaosArchive& FImplicitBVH::Serialize(FChaosArchive& Ar)
		{
			Ar << Objects;
			Ar << BVH;
			return Ar;
		}

		FChaosArchive& FImplicitBVHObject::Serialize(FChaosArchive& Ar)
		{
			Ar << Geometry;
			Ar << X;
			Ar << R;
			Ar << RootObjectIndex;
			return Ar;
		}

	}
}