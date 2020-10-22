// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rigs/RigHierarchyDefines.h"
#include "Rigs/RigHierarchyContainer.h"

////////////////////////////////////////////////////////////////////////////////
// FRigElementKeyCollection
////////////////////////////////////////////////////////////////////////////////

FRigElementKeyCollection FRigElementKeyCollection::MakeFromChildren(
	const FRigHierarchyContainer* InContainer,
	const FRigElementKey& InParentKey,
	bool bRecursive,
	bool bIncludeParent,
	uint8 InElementTypes)
{
	check(InContainer);

	FRigElementKeyCollection Collection;

	int32 Index = InContainer->GetIndex(InParentKey);
	if (Index == INDEX_NONE)
	{
		return Collection;
	}

	if (bIncludeParent)
	{
		Collection.AddUnique(InParentKey);
	}

	TArray<FRigElementKey> ParentKeys;
	ParentKeys.Add(InParentKey);

	bool bAddBones = (InElementTypes & (uint8)ERigElementType::Bone) == (uint8)ERigElementType::Bone;
	bool bAddControls = (InElementTypes & (uint8)ERigElementType::Control) == (uint8)ERigElementType::Control;
	bool bAddSpaces = (InElementTypes & (uint8)ERigElementType::Space) == (uint8)ERigElementType::Space;
	bool bAddCurves = (InElementTypes & (uint8)ERigElementType::Curve) == (uint8)ERigElementType::Curve;

	for (int32 ParentIndex = 0; ParentIndex < ParentKeys.Num(); ParentIndex++)
	{
		const FRigElementKey ParentKey = ParentKeys[ParentIndex];
		switch (ParentKey.Type)
		{
			case ERigElementType::Bone:
			{
				const FRigBoneHierarchy& Bones = InContainer->BoneHierarchy;
				const FRigBone& Bone = Bones[ParentKey.Name];

				if (bAddBones)
				{
					for (int32 Dependent : Bone.Dependents)
					{
						FRigElementKey DependentKey = Bones[Dependent].GetElementKey();
						Collection.AddUnique(DependentKey);

						if (bRecursive)
						{
							ParentKeys.AddUnique(DependentKey);
						}
					}
				}

				// fall through so that we get the spaces under bones as well
			}
			case ERigElementType::Space:
			case ERigElementType::Control:
			{
				if (bAddSpaces)
				{
					const FRigSpaceHierarchy& Spaces = InContainer->SpaceHierarchy;
					for (const FRigSpace& Space : Spaces)
					{
						if (Space.GetParentElementKey() == ParentKey)
						{
							FRigElementKey DependentKey = Space.GetElementKey();
							Collection.AddUnique(DependentKey);

							if (bRecursive)
							{
								ParentKeys.AddUnique(DependentKey);
							}
						}
					}
				}

				if (bAddControls && ParentKey.Type != ERigElementType::Bone)
				{
					const FRigControlHierarchy& Controls = InContainer->ControlHierarchy;
					for (const FRigControl& Control : Controls)
					{
						if (Control.GetSpaceElementKey() == ParentKey || 
							Control.GetParentElementKey() == ParentKey)
						{
							FRigElementKey DependentKey = Control.GetElementKey();
							Collection.AddUnique(DependentKey);

							if (bRecursive)
							{
								ParentKeys.AddUnique(DependentKey);
							}
						}
					}
				}
				break;
			}
			case ERigElementType::Curve:
			case ERigElementType::All:
			case ERigElementType::None:
			default:
			{
				break;
			}
		}
	}

	return Collection;
}

FRigElementKeyCollection FRigElementKeyCollection::MakeFromName(
	const FRigHierarchyContainer* InContainer,
	const FName& InPartialName,
	uint8 InElementTypes
)
{
	if (InPartialName.IsNone())
	{
		return MakeFromCompleteHierarchy(InContainer, InElementTypes);
	}

	check(InContainer);

	FRigElementKeyCollection Collection(InContainer->GetAllItems(true));
	Collection = Collection.FilterByType(InElementTypes);
	Collection = Collection.FilterByName(InPartialName);
	return Collection;
}

FRigElementKeyCollection FRigElementKeyCollection::MakeFromChain(
	const FRigHierarchyContainer* InContainer,
	const FRigElementKey& InFirstItem,
	const FRigElementKey& InLastItem,
	bool bReverse
)
{
	check(InContainer);

	FRigElementKeyCollection Collection;

	int32 FirstIndex = InContainer->GetIndex(InFirstItem);
	int32 LastIndex = InContainer->GetIndex(InLastItem);

	if (FirstIndex == INDEX_NONE || LastIndex == INDEX_NONE)
	{
		return Collection;
	}

	FRigElementKey LastKey = InLastItem;
	while (LastKey.IsValid() && LastKey != InFirstItem)
	{
		Collection.Keys.Add(LastKey);
		LastKey = InContainer->GetParentKey(LastKey);
	}

	if (LastKey != InFirstItem)
	{
		Collection.Reset();
	}
	else
	{
		Collection.AddUnique(InFirstItem);
	}

	if (!bReverse)
	{
		Algo::Reverse(Collection.Keys);
	}

	return Collection;
}

FRigElementKeyCollection FRigElementKeyCollection::MakeFromCompleteHierarchy(
	const FRigHierarchyContainer* InContainer,
	uint8 InElementTypes
)
{
	check(InContainer);

	FRigElementKeyCollection Collection(InContainer->GetAllItems(true));
	return Collection.FilterByType(InElementTypes);
}

FRigElementKeyCollection FRigElementKeyCollection::MakeUnion(const FRigElementKeyCollection& A, const FRigElementKeyCollection& B)
{
	FRigElementKeyCollection Collection;
	for (const FRigElementKey& Key : A)
	{
		Collection.Add(Key);
	}
	for (const FRigElementKey& Key : B)
	{
		Collection.AddUnique(Key);
	}
	return Collection;
}

FRigElementKeyCollection FRigElementKeyCollection::MakeIntersection(const FRigElementKeyCollection& A, const FRigElementKeyCollection& B)
{
	FRigElementKeyCollection Collection;
	for (const FRigElementKey& Key : A)
	{
		if (B.Contains(Key))
		{
			Collection.Add(Key);
		}
	}
	return Collection;
}

FRigElementKeyCollection FRigElementKeyCollection::MakeDifference(const FRigElementKeyCollection& A, const FRigElementKeyCollection& B)
{
	FRigElementKeyCollection Collection;
	for (const FRigElementKey& Key : A)
	{
		if (!B.Contains(Key))
		{
			Collection.Add(Key);
		}
	}
	return Collection;
}

FRigElementKeyCollection FRigElementKeyCollection::MakeReversed(const FRigElementKeyCollection& InCollection)
{
	FRigElementKeyCollection Reversed = InCollection;
	Algo::Reverse(Reversed.Keys);
	return Reversed;
}

FRigElementKeyCollection FRigElementKeyCollection::FilterByType(uint8 InElementTypes) const
{
	FRigElementKeyCollection Collection;
	for (const FRigElementKey& Key : *this)
	{
		if ((InElementTypes & (uint8)Key.Type) == (uint8)Key.Type)
		{
			Collection.Add(Key);
		}
	}
	return Collection;
}

FRigElementKeyCollection FRigElementKeyCollection::FilterByName(const FName& InPartialName) const
{
	FString SearchToken = InPartialName.ToString();

	FRigElementKeyCollection Collection;
	for (const FRigElementKey& Key : *this)
	{
		if (Key.Name == InPartialName)
		{
			Collection.Add(Key);
		}
		else if (Key.Name.ToString().Contains(SearchToken, ESearchCase::CaseSensitive, ESearchDir::FromStart))
		{
			Collection.Add(Key);
		}
	}
	return Collection;
}
