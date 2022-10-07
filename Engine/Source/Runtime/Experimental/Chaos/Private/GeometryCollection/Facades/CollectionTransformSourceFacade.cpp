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
	const FName FTransformSource::SourceGuidAttribute = "GuidID";
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
		Collection->AddAttribute<FString>(SourceGuidAttribute, TransformSourceGroup);
		Collection->AddAttribute<TSet<int32>>(SourceRootsAttribute, TransformSourceGroup, TransformDependency);
	}

	bool FTransformSource::HasFacade(const FManagedArrayCollection* Collection)
	{
		return Collection->HasGroup(TransformSourceGroup) &&
			Collection->FindAttribute<FString>(SourceNameAttribute, TransformSourceGroup) &&
			Collection->FindAttribute<FString>(SourceGuidAttribute, TransformSourceGroup) &&
			Collection->FindAttribute<TSet<int32>>(SourceRootsAttribute, TransformSourceGroup);
	}

	//
	//  Add Data
	//
	void FTransformSource::AddTransformSource(FManagedArrayCollection* Collection, const FString& InName, const FString& InGuid, const TSet<int32>& InRoots)
	{
		DefineSchema(Collection);

		int Idx = Collection->AddElements(1, TransformSourceGroup);
		Collection->ModifyAttribute<FString>(FTransformSource::SourceNameAttribute, TransformSourceGroup)[Idx] = InName;
		Collection->ModifyAttribute<FString>(FTransformSource::SourceGuidAttribute, TransformSourceGroup)[Idx] = InGuid;
		Collection->ModifyAttribute<TSet<int32>>(FTransformSource::SourceRootsAttribute, TransformSourceGroup)[Idx] = InRoots;
	}

	//
	//  Get Data
	//


	TSet<int32> FTransformSource::GetTransformSource(const FManagedArrayCollection* Collection, const FString& InName, const FString& InGuid)
	{
		if (Collection->HasGroup(TransformSourceGroup))
		{
			const TManagedArray<TSet<int32>>* Roots = Collection->FindAttribute<TSet<int32>>(SourceRootsAttribute, TransformSourceGroup);
			const TManagedArray<FString>* Guids = Collection->FindAttribute<FString>(SourceGuidAttribute, TransformSourceGroup);
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


