// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GeometryCollection.cpp: FGeometryCollection methods.
=============================================================================*/

#include "GeometryCollection/Facades/CollectionTransformSourceFacade.h"
#include "GeometryCollection/TransformCollection.h"

namespace GeometryCollection::Facades
{

	// Groups 
	const FName FTransformSource::TransformSourceGroup = "TransformSource";


	// Attributes
	const FName FTransformSource::SourceNameAttribute = "Name";
	const FName FTransformSource::SourceGuidAttribute = "Guid";
	const FName FTransformSource::SourceRootsAttribute = "Roots";

	FTransformSource::FTransformSource(FManagedArrayCollection* InCollection)
		: Self(InCollection)
	{
		DefineSchema(Self);
	}

	//
	//  Initialization
	//

	void FTransformSource::DefineSchema(FManagedArrayCollection* Collection)
	{
		FManagedArrayCollection::FConstructionParameters TransformDependency(FTransformCollection::TransformGroup);

		if (!Collection->HasGroup(TransformSourceGroup))
		{
			Collection->AddGroup(TransformSourceGroup);
		}
		Collection->AddAttribute<FString>(SourceNameAttribute, TransformSourceGroup);
		Collection->AddAttribute<FGuid>(SourceGuidAttribute, TransformSourceGroup);
		Collection->AddAttribute<TSet<int32>>(SourceRootsAttribute, TransformSourceGroup, TransformDependency);
	}

	bool FTransformSource::HasFacade(FManagedArrayCollection* Collection)
	{
		return Collection->HasGroup(TransformSourceGroup) &&
			Collection->FindAttribute<FString>(SourceNameAttribute, TransformSourceGroup) &&
			Collection->FindAttribute<FGuid>(SourceGuidAttribute, TransformSourceGroup) &&
			Collection->FindAttribute<TSet<int32>>(SourceRootsAttribute, TransformSourceGroup);
	}

	//
	//  Add Data
	//
	void FTransformSource::AddTransformSource(FManagedArrayCollection* Collection, const FString& InName, const FGuid& InGuid, const TSet<int32>& InRoots)
	{
		DefineSchema(Collection);

		int Idx = Collection->AddElements(1, TransformSourceGroup);
		Collection->ModifyAttribute<FString>(FTransformSource::SourceNameAttribute, TransformSourceGroup)[Idx] = InName;
		Collection->ModifyAttribute<FGuid>(FTransformSource::SourceGuidAttribute, TransformSourceGroup)[Idx] = InGuid;
		Collection->ModifyAttribute<TSet<int32>>(FTransformSource::SourceRootsAttribute, TransformSourceGroup)[Idx] = InRoots;
	}

	//
	//  Get Data
	//


	TSet<int32> FTransformSource::GetTransformSource(FManagedArrayCollection* Collection, const FString& InName, const FGuid& InGuid)
	{
		if (Collection->HasGroup(TransformSourceGroup))
		{
			const TManagedArray<TSet<int32>>* Roots = Collection->FindAttribute<TSet<int32>>(SourceRootsAttribute, TransformSourceGroup);
			const TManagedArray<FGuid>* Guids = Collection->FindAttribute<FGuid>(SourceGuidAttribute, TransformSourceGroup);
			const TManagedArray<FString>* Names = Collection->FindAttribute<FString>(SourceNameAttribute, TransformSourceGroup);
			if (Roots && Guids && Names)
			{
				int32 GroupNum = Collection->NumElements(TransformSourceGroup);
				for (int i = 0; i < GroupNum; i++)
				{
					if ((*Guids)[i]==InGuid && (*Names)[i].Equals(InName))
					{
						return (*Roots)[i];
					}
				}
			}
		}
		return TSet<int32>();
	}
};


