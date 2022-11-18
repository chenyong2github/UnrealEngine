// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/Facades/CollectionTransformFacade.h"
#include "GeometryCollection/TransformCollection.h"

namespace GeometryCollection::Facades
{
	// Group
	const FName FTransformFacade::TransformGroup = FTransformCollection::TransformGroup;

	// Attributes
	const FName FTransformFacade::ParentAttribute = FTransformCollection::ParentAttribute;
	const FName FTransformFacade::ChildrenAttribute = FTransformCollection::ChildrenAttribute;
	const FName FTransformFacade::TransformAttribute = FTransformCollection::TransformAttribute;


	FTransformFacade::FTransformFacade(FManagedArrayCollection& InCollection)
		: Self(InCollection)
		, Parent(InCollection, ChildrenAttribute, TransformGroup)
		, Children(InCollection, ParentAttribute, TransformGroup)
		, Transform(InCollection, TransformAttribute, TransformGroup)
	{}

	bool FTransformFacade::IsValid() const
	{
		return Parent.IsValid() && Children.IsValid() && Transform.IsValid();
	}

	TSet<int32> FTransformFacade::GetRootIndices() const
	{
		TSet<int32> Roots;
		if (Parent.IsValid())
		{
			const TManagedArray<int32>& Parents = Parent.Get();
			for (int i = 0; i < Parents.Num(); i++)
			{
				Roots.Add(i);
			}
		}
		return Roots;
	}

}