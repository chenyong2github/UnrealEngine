// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/Facades/CollectionTransformFacade.h"
#include "GeometryCollection/TransformCollection.h"
#include "GeometryCollection/Facades/CollectionHierarchyFacade.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"

namespace GeometryCollection::Facades
{
	FCollectionTransformFacade::FCollectionTransformFacade(FManagedArrayCollection& InCollection)
		: ParentAttribute(InCollection, FTransformCollection::ParentAttribute, FTransformCollection::TransformGroup)
		, ChildrenAttribute(InCollection, FTransformCollection::ChildrenAttribute, FTransformCollection::TransformGroup)
		, TransformAttribute(InCollection, FTransformCollection::TransformAttribute, FTransformCollection::TransformGroup)
	{}

	FCollectionTransformFacade::FCollectionTransformFacade(const FManagedArrayCollection& InCollection)
		: ParentAttribute(InCollection, FTransformCollection::ParentAttribute, FTransformCollection::TransformGroup)
		, ChildrenAttribute(InCollection, FTransformCollection::ChildrenAttribute, FTransformCollection::TransformGroup)
		, TransformAttribute(InCollection, FTransformCollection::TransformAttribute, FTransformCollection::TransformGroup)
	{}

	bool FCollectionTransformFacade::IsValid() const
	{
		return ParentAttribute.IsValid() && ChildrenAttribute.IsValid() && TransformAttribute.IsValid();
	}

	TArray<int32> FCollectionTransformFacade::GetRootIndices() const
	{
		return Chaos::Facades::FCollectionHierarchyFacade::GetRootIndices(ParentAttribute);
	}

	TArray<FTransform> FCollectionTransformFacade::ComputeCollectionSpaceTransforms() const
	{
		TArray<FTransform> OutTransforms;

		const TManagedArray<FTransform>& BoneTransforms = TransformAttribute.Get();
		const TManagedArray<int32>& Parents = ParentAttribute.Get();

		GeometryCollectionAlgo::GlobalMatrices(BoneTransforms, Parents, OutTransforms);

		return OutTransforms;
	}

	FTransform FCollectionTransformFacade::ComputeCollectionSpaceTransform(int32 BoneIdx) const
	{
		const TManagedArray<FTransform>& BoneTransforms = TransformAttribute.Get();
		const TManagedArray<int32>& Parents = ParentAttribute.Get();

		return GeometryCollectionAlgo::GlobalMatrix(BoneTransforms, Parents, BoneIdx);
	}

	void FCollectionTransformFacade::SetPivot(const FTransform& InTransform)
	{
		Transform(InTransform.Inverse());
	}

	void FCollectionTransformFacade::Transform(const FTransform& InTransform)
	{
		// Update only root transforms
		const TArray<int32>& RootIndices = GetRootIndices();

		TManagedArray<FTransform>& Transforms = TransformAttribute.Modify();

		for (int32 Idx : RootIndices)
		{
			Transforms[Idx] = Transforms[Idx] * InTransform;
		}
	}

	void FCollectionTransformFacade::Transform(const FTransform& InTransform, const TArray<int32>& InSelection)
	{
		ensureAlwaysMsgf(false, TEXT("NOT YET IMPLEMENTED"));
	}

}