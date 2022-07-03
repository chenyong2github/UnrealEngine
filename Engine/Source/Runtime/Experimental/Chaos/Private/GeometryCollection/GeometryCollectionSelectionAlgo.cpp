// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GeometryCollection.cpp: FGeometryCollection methods.
=============================================================================*/

#include "GeometryCollection/GeometryCollectionSelectionAlgo.h"

namespace GeometryCollection
{

	// Groups 
	const FName FSelectionAlgo::UnboundGroup = "Unbound";
	const FName FSelectionAlgo::WeightedUnboundGroup = "WeightedUnbound";
	const FName FSelectionAlgo::BoundGroup = "Bound";
	const FName FSelectionAlgo::WeightedBoundGroup = "WeightedBound";

	// Attributes
	const FName FSelectionAlgo::IndexAttribute = "Index";
	const FName FSelectionAlgo::WeightAttribute = "Weights";
	const FName FSelectionAlgo::BoneIndexAttribute = "BoneIndex";

	FSelectionAlgo::FSelectionAlgo(FManagedArrayCollection* InCollection)
		: Self(InCollection)
	{}

	//
	//  Initialization
	//

	void FSelectionAlgo::InitUnboundedGroup(FManagedArrayCollection* Collection, FName GroupName, FName DependencyGroup)
	{
		if (!Collection->HasGroup(GroupName))
		{
			Collection->AddAttribute<TArray<int32>>(FSelectionAlgo::IndexAttribute, GroupName, { DependencyGroup });
		}
		ensure(Collection->FindAttributeTyped<TArray<int32>>(FSelectionAlgo::IndexAttribute, GroupName) != nullptr);
	}

	void FSelectionAlgo::InitWeightedUnboundedGroup(FManagedArrayCollection* Collection, FName GroupName, FName DependencyGroup)
	{
		if (!Collection->HasGroup(GroupName))
		{
			Collection->AddAttribute<TArray<int32>>(FSelectionAlgo::IndexAttribute, GroupName, { DependencyGroup });
			Collection->AddAttribute<TArray<float>>(FSelectionAlgo::WeightAttribute, GroupName);
		}
		ensure(Collection->FindAttributeTyped<TArray<int32>>(FSelectionAlgo::IndexAttribute, GroupName) != nullptr);
		ensure(Collection->FindAttributeTyped<TArray<float>>(FSelectionAlgo::WeightAttribute, GroupName) != nullptr);
	}

	void FSelectionAlgo::InitBoundedGroup(FManagedArrayCollection* Collection, FName GroupName, FName DependencyGroup, FName BoneDependencyGroup)
	{
		if (!Collection->HasGroup(GroupName))
		{
			Collection->AddAttribute<TArray<int32>>(FSelectionAlgo::IndexAttribute, GroupName, { DependencyGroup });
			Collection->AddAttribute<int32>(FSelectionAlgo::BoneIndexAttribute, GroupName, { BoneDependencyGroup });
		}
		ensure(Collection->FindAttributeTyped<TArray<int32>>(FSelectionAlgo::IndexAttribute, GroupName) != nullptr);
		ensure(Collection->FindAttributeTyped<int32>(FSelectionAlgo::BoneIndexAttribute, GroupName) != nullptr);
	}

	void FSelectionAlgo::InitWeightedBoundedGroup(FManagedArrayCollection* Collection, FName GroupName, FName DependencyGroup, FName BoneDependencyGroup)
	{
		if (!Collection->HasGroup(GroupName))
		{
			Collection->AddAttribute<TArray<int32>>(FSelectionAlgo::IndexAttribute, GroupName, { DependencyGroup });
			Collection->AddAttribute<TArray<float>>(FSelectionAlgo::WeightAttribute, GroupName);
			Collection->AddAttribute<int32>(FSelectionAlgo::BoneIndexAttribute, GroupName, { BoneDependencyGroup });
		}
		ensure(Collection->FindAttributeTyped<TArray<int32>>(FSelectionAlgo::IndexAttribute, GroupName) != nullptr);
		ensure(Collection->FindAttributeTyped<TArray<float>>(FSelectionAlgo::WeightAttribute, GroupName) != nullptr);
		ensure(Collection->FindAttributeTyped<int32>(FSelectionAlgo::BoneIndexAttribute, GroupName) != nullptr);
	}


	//
	//  AddSelection
	//

	FSelectionAlgo::FSelectionKey FSelectionAlgo::AddSelection(FManagedArrayCollection* Collection, const TArray<int32>& InIndices, FName DependencyGroup)
	{
		FName GroupName(FSelectionAlgo::UnboundGroup.ToString() + "_" + DependencyGroup.ToString());
		InitUnboundedGroup(Collection, GroupName, DependencyGroup);

		int Idx = Collection->AddElements(1, GroupName);
		Collection->ModifyAttribute<TArray<int32>>(FSelectionAlgo::IndexAttribute, GroupName)[Idx] = InIndices;
		return FSelectionKey(Idx, FSelectionAlgo::UnboundGroup);
	}

	FSelectionAlgo::FSelectionKey FSelectionAlgo::AddSelection(FManagedArrayCollection* Collection, const TArray<int32>& InIndices, const TArray<float>& InWeights, FName DependencyGroup)
	{
		FName GroupName(FSelectionAlgo::WeightedUnboundGroup.ToString() + "_" + DependencyGroup.ToString());
		InitWeightedUnboundedGroup(Collection, GroupName, DependencyGroup);

		int Idx = Collection->AddElements(1, GroupName);
		Collection->ModifyAttribute<TArray<int32>>(FSelectionAlgo::IndexAttribute, GroupName)[Idx] = InIndices;
		Collection->ModifyAttribute<TArray<float>>(FSelectionAlgo::WeightAttribute, GroupName)[Idx] = InWeights;
		return FSelectionKey(Idx, GroupName);
	}

	FSelectionAlgo::FSelectionKey FSelectionAlgo::AddSelection(FManagedArrayCollection* Collection, const int32 InBoneIndex, const TArray<int32>& InIndices, FName DependencyGroup, FName BoneDependencyGroup)
	{
		FName GroupName(FSelectionAlgo::BoundGroup.ToString() + "_" + DependencyGroup.ToString());
		InitBoundedGroup(Collection, GroupName, DependencyGroup, BoneDependencyGroup);

		int Idx = Collection->AddElements(1, GroupName);
		Collection->ModifyAttribute<TArray<int32>>(FSelectionAlgo::IndexAttribute, GroupName)[Idx] = InIndices;
		Collection->ModifyAttribute<int32>(FSelectionAlgo::BoneIndexAttribute, GroupName)[Idx] = InBoneIndex;
		return FSelectionKey(Idx, GroupName);
	}

	FSelectionAlgo::FSelectionKey FSelectionAlgo::AddSelection(FManagedArrayCollection* Collection, const int32 InBoneIndex, const TArray<int32>& InIndices, const TArray<float>& InWeights, FName DependencyGroup, FName BoneDependencyGroup)
	{
		FName GroupName(FSelectionAlgo::WeightedBoundGroup.ToString() + "_" + DependencyGroup.ToString());
		InitWeightedBoundedGroup(Collection, GroupName, DependencyGroup, BoneDependencyGroup);

		int Idx = Collection->AddElements(1, GroupName);
		Collection->ModifyAttribute<TArray<int32>>(FSelectionAlgo::IndexAttribute, GroupName)[Idx] = InIndices;
		Collection->ModifyAttribute<TArray<float>>(FSelectionAlgo::WeightAttribute, GroupName)[Idx] = InWeights;
		Collection->ModifyAttribute<int32>(FSelectionAlgo::BoneIndexAttribute, GroupName)[Idx] = InBoneIndex;
		return FSelectionKey(Idx, GroupName);
	}

	//
	//  GetSelection
	//


	void FSelectionAlgo::GetSelection(const FManagedArrayCollection* Collection, const FSelectionAlgo::FSelectionKey& Key, TArray<int32>& OutIndices)
	{
		if (Collection->HasGroup(Key.GroupName) && 0 <= Key.Index && Key.Index < Collection->NumElements(Key.GroupName))
		{
			if (Collection->FindAttribute<TArray<int32>>(FSelectionAlgo::IndexAttribute, Key.GroupName))
				OutIndices = Collection->GetAttribute<TArray<int32>>(FSelectionAlgo::IndexAttribute, Key.GroupName)[Key.Index];
		}
	}

	void FSelectionAlgo::GetSelection(const FManagedArrayCollection* Collection, const FSelectionAlgo::FSelectionKey& Key, TArray<int32>& OutIndices, TArray<float>& OutWeights)
	{
		if (Collection->HasGroup(Key.GroupName) && 0 <= Key.Index && Key.Index < Collection->NumElements(Key.GroupName))
		{
			if (Collection->FindAttribute<TArray<int32>>(FSelectionAlgo::IndexAttribute, Key.GroupName))
				OutIndices = Collection->GetAttribute<TArray<int32>>(FSelectionAlgo::IndexAttribute, Key.GroupName)[Key.Index];
			if (Collection->FindAttribute<TArray<float>>(FSelectionAlgo::WeightAttribute, Key.GroupName))
				OutWeights = Collection->GetAttribute<TArray<float>>(FSelectionAlgo::WeightAttribute, Key.GroupName)[Key.Index];
		}
	}

	void FSelectionAlgo::GetSelection(const FManagedArrayCollection* Collection, const FSelectionAlgo::FSelectionKey& Key, int32& OutBoneIndex, TArray<int32>& OutIndices)
	{
		if (Collection->HasGroup(Key.GroupName) && 0 <= Key.Index && Key.Index < Collection->NumElements(Key.GroupName))
		{
			if (Collection->FindAttribute<TArray<int32>>(FSelectionAlgo::IndexAttribute, Key.GroupName))
				OutIndices = Collection->GetAttribute<TArray<int32>>(FSelectionAlgo::IndexAttribute, Key.GroupName)[Key.Index];
			if (Collection->FindAttribute<int32>(FSelectionAlgo::BoneIndexAttribute, Key.GroupName))
				OutBoneIndex = Collection->GetAttribute<int32>(FSelectionAlgo::BoneIndexAttribute, Key.GroupName)[Key.Index];
		}
	}

	void FSelectionAlgo::GetSelection(const FManagedArrayCollection* Collection, const FSelectionAlgo::FSelectionKey& Key, int32& OutBoneIndex, TArray<int32>& OutIndices, TArray<float>& OutWeights)
	{
		if (Collection->HasGroup(Key.GroupName) && 0 <= Key.Index && Key.Index < Collection->NumElements(Key.GroupName))
		{
			if (Collection->FindAttribute<TArray<int32>>(FSelectionAlgo::IndexAttribute, Key.GroupName))
				OutIndices = Collection->GetAttribute<TArray<int32>>(FSelectionAlgo::IndexAttribute, Key.GroupName)[Key.Index];
			if (Collection->FindAttribute<TArray<float>>(FSelectionAlgo::WeightAttribute, Key.GroupName))
				OutWeights = Collection->GetAttribute<TArray<float>>(FSelectionAlgo::WeightAttribute, Key.GroupName)[Key.Index];
			if (Collection->FindAttribute<int32>(FSelectionAlgo::BoneIndexAttribute, Key.GroupName))
				OutBoneIndex = Collection->GetAttribute<int32>(FSelectionAlgo::BoneIndexAttribute, Key.GroupName)[Key.Index];
		}
	}

};


