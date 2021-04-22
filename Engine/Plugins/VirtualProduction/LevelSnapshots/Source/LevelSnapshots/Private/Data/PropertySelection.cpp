// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/PropertySelection.h"

FLevelSnapshotPropertyChain FLevelSnapshotPropertyChain::MakeAppended(const FProperty* Property) const
{
	FLevelSnapshotPropertyChain Result = *this;
	Result.AppendInline(Property);
	return Result;
}

void FLevelSnapshotPropertyChain::AppendInline(const FProperty* Property)
{
	if (ensure(Property))
	{
		// Sadly const_cast is required because parent class FArchiveSerializedPropertyChain requires it. However, we'll never actually modify the property.
		PushProperty(const_cast<FProperty*>(Property), false);
	}
}

bool FLevelSnapshotPropertyChain::EqualsSerializedProperty(const FArchiveSerializedPropertyChain* ContainerChain, const FProperty* LeafProperty) const
{
	check(GetNumProperties() > 0);
	
	const bool bHaveSameLeaf = LeafProperty == GetPropertyFromStack(0);
	if (!ContainerChain)
	{
		return bHaveSameLeaf;
	}
	
	const bool bHaveSameChainLength = GetNumProperties() + 1 == ContainerChain->GetNumProperties();
	if (!bHaveSameLeaf
        || !bHaveSameChainLength)
	{
		return false;
	}
	
	// Walk up the chain and compare every element
	for (int32 i = 0; i < GetNumProperties(); ++i)
	{
		if (ContainerChain->GetPropertyFromRoot(i) != GetPropertyFromRoot(i))
		{
			return false;
		}
	}

	return true;
}

bool FLevelSnapshotPropertyChain::IsEmpty() const
{
	return GetNumProperties() == 0;
}

bool FPropertySelection::IsPropertySelected(const FArchiveSerializedPropertyChain* ContainerChain, const FProperty* LeafProperty) const
{
	return FindPropertyChain(ContainerChain, LeafProperty) != INDEX_NONE;
}
bool FPropertySelection::IsEmpty() const
{
	return SelectedProperties.Num() == 0;
}

void FPropertySelection::AddProperty(const FLevelSnapshotPropertyChain& SelectedProperty)
{
	if (ensure(SelectedProperty.GetNumProperties() != 0))
	{
		SelectedLeafProperties.Add(SelectedProperty.GetPropertyFromStack(0));
	
		SelectedProperties.Add(SelectedProperty);
	}
}
void FPropertySelection::RemoveProperty(const FArchiveSerializedPropertyChain* ContainerChain, const FProperty* LeafProperty)
{
	// We won't modify the property. TFieldPath interface is a bit cumbersome here.
	SelectedLeafProperties.Remove(const_cast<FProperty*>(LeafProperty));
	
	const int32 Index = FindPropertyChain(ContainerChain, LeafProperty);
	if (Index != INDEX_NONE)
	{
		SelectedProperties.RemoveAtSwap(Index);
	}
}

const TArray<TFieldPath<FProperty>>& FPropertySelection::GetSelectedLeafProperties() const
{
	return SelectedLeafProperties;
}

int32 FPropertySelection::FindPropertyChain(const FArchiveSerializedPropertyChain* ContainerChain, const FProperty* LeafProperty) const
{
	if (!ensure(LeafProperty))
	{
		return false;
	}

	for (int32 i = 0; i < SelectedProperties.Num(); ++i)
	{
		const FLevelSnapshotPropertyChain& SelectedChain = SelectedProperties[i];
		if (SelectedChain.EqualsSerializedProperty(ContainerChain, LeafProperty))
		{
			return i;
		}
	}
	
	return INDEX_NONE;
}
