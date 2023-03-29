// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Core.h"
#include "Chaos/BoundingVolumeHierarchy.h"
#include "Chaos/ImplicitFwd.h"
#include "Chaos/Serializable.h"

namespace Chaos
{
	class FChaosArchive;

	namespace Private
	{
		class FImplicitBVH;
		class FImplicitObjectUnion;

		// An entry in the ImplicitObject BVH that holds the leaf geometry and transform
		class FImplicitBVHObject
		{
		public:
			friend class FImplicitBVH;

			friend FChaosArchive& operator<<(FChaosArchive& Ar, FImplicitBVHObject& BVHObjecty);

			FImplicitBVHObject();
			FImplicitBVHObject(const TSerializablePtr<FImplicitObject>& InGeometry, const FVec3& InX, const FRotation3& InR, const FAABB3& InBounds, const int32 InRootObjectIndex, const int32 InObjectIndex);

			const FImplicitObject* GetGeometry() const { return Geometry.Get(); }

			const FVec3f& GetX() const { return X; }

			const FRotation3f& GetR() const { return R; }

			const FAABB3f& GetBounds() const { return Bounds; }

			FRigidTransform3f GetTransformf() const { return FRigidTransform3f(GetX(), GetR()); }

			FRigidTransform3 GetTransform() const { return FRigidTransform3(FVec3(GetX()), FRotation3(GetR())); }

			// A unique index for this object in the hierarchy. E.g., if the same FImplicirtObject is referenced 
			// multiple times in the hierarchy in a union of transformed objects, each will have a different ObjectIndex 
			// (it is the index into the pre-order depth first traversal).
			int32 GetObjectIndex() const { return ObjectIndex; }

			// The index of our most distant ancestor. I.e., the index in the root Union. This is used to map
			// each object to a ShapeInstance.
			int32 GetRootObjectIndex() const { return RootObjectIndex; }

		private:
			FChaosArchive& Serialize(FChaosArchive& Ar);

			// Transform and bounds in the space of the BVH owner (Union Implicit)
			FRotation3f R;
			FVec3f X;
			FAABB3f Bounds;

			// The leaf geometry stripped of decorators (but not Instanced or Scaled)
			TSerializablePtr<FImplicitObject> Geometry;

			// The index of our ancestor in the array of RootObjects that was provided when creating the BVH
			int32 RootObjectIndex;

			// Our index in the hierarchy. This could be used to uniquely identity copies of the same implicit in the hierarchy
			int32 ObjectIndex;
		};

		// A Bounding Volume Hierarchy of a set of Implicit Objects
		class FImplicitBVH
		{
		public:
			using FObjects = TArray<FImplicitBVHObject>;
			using FLeaf = TArray<int32>;
			using FBVH = TBoundingVolumeHierarchy<FObjects, FLeaf>;

			friend FChaosArchive& operator<<(FChaosArchive& Ar, FImplicitBVH& BVH);

			// Amke an empty BVH (for serialization only)
			static TUniquePtr<FImplicitBVH> MakeEmpty();

			// Utility for processing the hierarchy
			static int32 CountLeafObjects(const TArrayView<const TUniquePtr<FImplicitObject>>& InRootObjects);
			static FObjects CollectLeafObjects(const TArrayView<const TUniquePtr<FImplicitObject>>& InRootObjects);

			// Create a BVH around a set of ImplicitObjects. Usually these are the immediate child elements of an FImplcitObjectUnion
			// TryMake will then recurse into the geometry hierachy and add all descendents to the BVH. Will return null if the 
			// number of descendents is less that MinObjscts.
			static TUniquePtr<FImplicitBVH> TryMake(const TArrayView<const TUniquePtr<FImplicitObject>>& InRootObjects, const int32 MinObjects, const int32 InMaxBVHDepth);

			~FImplicitBVH();

			int32 NumObjects() const { return Objects.Num(); }

			const FImplicitBVHObject& GetObject(const int32 ObjectIndex) const { return Objects[ObjectIndex]; }

			const FImplicitObject* GetGeometry(const int32 ObjectIndex) const { return Objects[ObjectIndex].GetGeometry(); }

			const FVec3f& GetX(const int32 ObjectIndex) const { return Objects[ObjectIndex].GetX(); }

			const FRotation3f& GetR(const int32 ObjectIndex) const { return Objects[ObjectIndex].GetR(); }

			const FAABB3f& GetBounds(const int32 ObjectIndex) const { return Objects[ObjectIndex].GetBounds(); }

			FRigidTransform3f GetTransformf(const int32 ObjectIndex) const { return Objects[ObjectIndex].GetTransformf(); }

			FRigidTransform3 GetTransform(const int32 ObjectIndex) const { return Objects[ObjectIndex].GetTransform(); }

			int32 GetRootObjectIndex(const int32 ObjectIndex) const { return Objects[ObjectIndex].GetRootObjectIndex(); }
			int32 GetObjectIndex(const int32 ObjectIndex) const { return Objects[ObjectIndex].GetObjectIndex(); }

			const FBVH& GetBVH() const { return BVH; }

			// @param Visitor void<int ObjectIndex> where ObjectIndex is an index into the array of objects in the hierarchy
			// for use with GetObject(ObjectIndex), GetGeometry(ObjectIndex), etc.
			template<typename TVisitor>
			void VisitAllIntersections(const FAABB3& LocalBounds, const TVisitor& Visitor) const
			{
				// Each objects can be in multiple leafs, so we keep track of whichones we have visited
				TArray<bool> ObjectVisited;
				ObjectVisited.AddZeroed(NumObjects());
			
				const auto& LeafVisitor = [this, &ObjectVisited, &Visitor](const TArray<int32>& Indices)
				{
					for (const int32 Index : Indices)
					{
						if (!ObjectVisited[Index])
						{
							ObjectVisited[Index] = true;
							Visitor(Index);
						}
					}
				};

				BVH.VisitAllIntersections(LocalBounds, LeafVisitor);
			}

		private:
			FImplicitBVH();
			FImplicitBVH(FObjects&& InObjects, const int32 InMaxBVHDepth);
			FChaosArchive& Serialize(FChaosArchive& Ar);

			// Initialize the BVH from the specified set of children.
			// NOTE: InChildren should be immediate children of the BVH owner, not further-removed descendents
			void Init(FObjects&& InObjects, const int32 InMaxBVHDepth);

			// A BVH leaf holds an array of indices into the Objects array
			FObjects Objects;
			FBVH BVH;
		};

		class FImplicitCache
		{
		public:

		private:
			TUniquePtr<FImplicitBVH> BVH;
		};
	}

	///////////////////////////////////////////////////////////////////////////////////////////////
	///////////////////////////////////////////////////////////////////////////////////////////////
	// 
	// TBoundingVolumeHierarchy Template API
	// 
	///////////////////////////////////////////////////////////////////////////////////////////////
	///////////////////////////////////////////////////////////////////////////////////////////////

	template<>
	inline bool HasBoundingBox(const TArray<Private::FImplicitBVHObject>& Objects, const int32 ObjectIndex)
	{
		return Objects[ObjectIndex].GetGeometry()->HasBoundingBox();
	}

	template<class T, int d>
	inline const TAABB<T, d> GetWorldSpaceBoundingBox(const TArray<Private::FImplicitBVHObject>& Objects, const int32 ObjectIndex, const TMap<int32, TAABB<T, d>>& WorldSpaceBoxes)
	{
		return TAABB<T, d>(Objects[ObjectIndex].GetBounds());
	}

	template<class T, int d>
	void ComputeAllWorldSpaceBoundingBoxes(const TArray<Private::FImplicitBVHObject>& Objects, const TArray<int32>& AllObjects, const bool bUseVelocity, const T Dt, TMap<int32, TAABB<T, d>>& WorldSpaceBoxes)
	{
	}

}