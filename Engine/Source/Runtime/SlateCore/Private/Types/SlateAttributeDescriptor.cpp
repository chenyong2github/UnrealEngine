// Copyright Epic Games, Inc. All Rights Reserved.

#include "Types/SlateAttributeDescriptor.h"
#include "Types/SlateAttribute.h"
#include "Containers/ArrayView.h"


FSlateAttributeDescriptor::FInitializer::FAttributeEntry::FAttributeEntry(FSlateAttributeDescriptor& InDescriptor, int32 InAttributeIndex)
	: Descriptor(InDescriptor)
	, AttributeIndex(InAttributeIndex)
{}

	
FSlateAttributeDescriptor::FInitializer::FAttributeEntry& FSlateAttributeDescriptor::FInitializer::FAttributeEntry::UpdatePrerequisite(FName Prerequisite)
{
	if (Descriptor.Attributes.IsValidIndex(AttributeIndex))
	{
		Descriptor.SetPrerequisite(Descriptor.Attributes[AttributeIndex], Prerequisite, false);
	}
	return *this;
}


FSlateAttributeDescriptor::FInitializer::FAttributeEntry& FSlateAttributeDescriptor::FInitializer::FAttributeEntry::UpdateDependency(FName Dependency)
{
	if (Descriptor.Attributes.IsValidIndex(AttributeIndex))
	{
		Descriptor.SetPrerequisite(Descriptor.Attributes[AttributeIndex], Dependency, true);
	}
	return *this;
}


FSlateAttributeDescriptor::FInitializer::FAttributeEntry& FSlateAttributeDescriptor::FInitializer::FAttributeEntry::UpdateWhenCollapsed()
{
	if (Descriptor.Attributes.IsValidIndex(AttributeIndex))
	{
		Descriptor.SetUpdateWhenCollapsed(Descriptor.Attributes[AttributeIndex], true);
	}
	return *this;
}


FSlateAttributeDescriptor::FInitializer::FInitializer(FSlateAttributeDescriptor& InDescriptor)
	: Descriptor(InDescriptor)
{
}

FSlateAttributeDescriptor::FInitializer::FInitializer(FSlateAttributeDescriptor& InDescriptor, const FSlateAttributeDescriptor& ParentDescriptor)
	: Descriptor(InDescriptor)
{
	InDescriptor.Attributes = ParentDescriptor.Attributes;
}


FSlateAttributeDescriptor::FInitializer::~FInitializer()
{
	// Update the sort order for the item that have prerequisite.
	//Because adding the attribute is not required at the moment
	//try to not change the order in which they were added

	struct FPrerequisiteSort
	{
		FPrerequisiteSort() = default;
		FPrerequisiteSort(int32 A, int32 B, int32 InDepth) : AttributeIndex(A), PrerequisitesIndex(B), Depth(InDepth) {}
		int32 AttributeIndex;
		int32 PrerequisitesIndex;
		int32 Depth = -1;

		void CalculateDepth(TArrayView<FPrerequisiteSort> Prerequisites)
		{
			if (Depth < 0)
			{
				check(PrerequisitesIndex != INDEX_NONE);
				if (Prerequisites[PrerequisitesIndex].Depth < 0)
				{
					// calculate the Depth recursively
					Prerequisites[PrerequisitesIndex].CalculateDepth(Prerequisites);
				}
				Depth = Prerequisites[PrerequisitesIndex].Depth + 1;
			}
		}
	};
	struct FPrerequisiteSortPredicate
	{
		const TArray<FAttribute>& Attrbutes;
		bool operator()(const FPrerequisiteSort& A, const FPrerequisiteSort& B) const
		{
			if (A.Depth != B.Depth)
			{
				return A.Depth < B.Depth;
			}
			if (A.PrerequisitesIndex == B.PrerequisitesIndex)
			{
				return Attrbutes[A.AttributeIndex].SortOrder < Attrbutes[B.AttributeIndex].SortOrder;
			}

			const int32 SortA = A.PrerequisitesIndex != INDEX_NONE ? Attrbutes[A.PrerequisitesIndex].SortOrder : Attrbutes[A.AttributeIndex].SortOrder;
			const int32 SortB = B.PrerequisitesIndex != INDEX_NONE ? Attrbutes[B.PrerequisitesIndex].SortOrder : Attrbutes[B.AttributeIndex].SortOrder;
			return SortA < SortB;
		}
	};

	TArray<FPrerequisiteSort, TInlineAllocator<16>> Prerequisites;
	Prerequisites.Reserve(Descriptor.Attributes.Num());

	bool bHavePrerequisite = false;
	for (int32 Index = 0; Index < Descriptor.Attributes.Num(); ++Index)
	{
		FAttribute& Attribute = Descriptor.Attributes[Index];
		Attribute.SortOrder = DefaultSortOrder(Attribute.Offset);

		if (!Attribute.Prerequisite.IsNone())
		{
			// Find the Prerequisite index
			const FName Prerequisite = Attribute.Prerequisite;
			const int32 PrerequisiteIndex = Descriptor.Attributes.IndexOfByPredicate([Prerequisite](const FAttribute& Other) { return Other.Name == Prerequisite; });
			if (ensureAlwaysMsgf(Descriptor.Attributes.IsValidIndex(PrerequisiteIndex), TEXT("The Prerequisite '%s' doesn't exist"), *Prerequisite.ToString()))
			{
				Prerequisites.Emplace(Index, PrerequisiteIndex, -1);
				Descriptor.Attributes[PrerequisiteIndex].bIsADependencyForSomeoneElse = true;
				bHavePrerequisite = true;
			}
			else
			{
				Prerequisites.Emplace(Index, INDEX_NONE, 0);
			}
		}
		else
		{
			Prerequisites.Emplace(Index, INDEX_NONE, 0);
		}
	}

	if (bHavePrerequisite)
	{
		// Get the depth order
		for (FPrerequisiteSort& PrerequisiteSort : Prerequisites)
		{
			PrerequisiteSort.CalculateDepth(Prerequisites);
		}

		Prerequisites.Sort(FPrerequisiteSortPredicate{ Descriptor.Attributes });
		int32 PreviousPrerequisiteIndex = INDEX_NONE;
		int32 IncreaseCount = 1;
		for (const FPrerequisiteSort& Element : Prerequisites)
		{
			if (Element.PrerequisitesIndex != INDEX_NONE)
			{
				if (PreviousPrerequisiteIndex == Element.PrerequisitesIndex)
				{
					++IncreaseCount;
				}
				Descriptor.Attributes[Element.AttributeIndex].SortOrder = Descriptor.Attributes[Element.PrerequisitesIndex].SortOrder + IncreaseCount;
			}
			PreviousPrerequisiteIndex = Element.PrerequisitesIndex;
		}
	}
}


FSlateAttributeDescriptor::FInitializer::FAttributeEntry FSlateAttributeDescriptor::FInitializer::AddMemberAttribute(FName AttributeName, OffsetType Offset, const FInvalidateWidgetReasonAttribute& Reason)
{
	return Descriptor.AddMemberAttribute(AttributeName, Offset, Reason);
}


FSlateAttributeDescriptor::FInitializer::FAttributeEntry FSlateAttributeDescriptor::FInitializer::AddMemberAttribute(FName AttributeName, OffsetType Offset, FInvalidateWidgetReasonAttribute&& Reason)
{
	return Descriptor.AddMemberAttribute(AttributeName, Offset, MoveTemp(Reason));
}


void FSlateAttributeDescriptor::FInitializer::OverrideInvalidationReason(FName AttributeName, const FInvalidateWidgetReasonAttribute& Reason)
{
	Descriptor.OverrideInvalidationReason(AttributeName, Reason);
}


void FSlateAttributeDescriptor::FInitializer::SetUpdateWhenCollapsed(FName AttributeName, bool bUpdateWhenCollapsed)
{
	FAttribute* Attribute = Descriptor.FindAttribute(AttributeName);
	if (ensureAlwaysMsgf(Attribute, TEXT("The attribute named '%s' doesn't exist"), *AttributeName.ToString()))
	{
		Descriptor.SetUpdateWhenCollapsed(*Attribute, bUpdateWhenCollapsed);
	}
}


void FSlateAttributeDescriptor::FInitializer::OverrideInvalidationReason(FName AttributeName, FInvalidateWidgetReasonAttribute&& Reason)
{
	Descriptor.OverrideInvalidationReason(AttributeName, MoveTemp(Reason));
}


FSlateAttributeDescriptor::FAttribute const& FSlateAttributeDescriptor::GetAttributeAtIndex(int32 Index) const
{
	check(Attributes.IsValidIndex(Index));
	FSlateAttributeDescriptor::FAttribute const& Result = Attributes[Index];
	return Result;
}


FSlateAttributeDescriptor::FAttribute const* FSlateAttributeDescriptor::FindAttribute(FName AttributeName) const
{
	return Attributes.FindByPredicate([AttributeName](const FAttribute& Other) { return Other.Name == AttributeName; });
}


int32 FSlateAttributeDescriptor::IndexOfMemberAttribute(OffsetType AttributeOffset) const
{
	int32 FoundIndex = Attributes.IndexOfByPredicate([AttributeOffset](const FAttribute& Other) { return Other.Offset == AttributeOffset; });
	check(FoundIndex == INDEX_NONE || Attributes[FoundIndex].bIsMemberAttribute);
	return FoundIndex;
}


int32 FSlateAttributeDescriptor::IndexOfMemberAttribute(FName AttributeName) const
{
	int32 FoundIndex = Attributes.IndexOfByPredicate([AttributeName](const FAttribute& Other) { return Other.Name == AttributeName; });
	if (ensure(FoundIndex == INDEX_NONE || Attributes[FoundIndex].bIsMemberAttribute))
	{
		return FoundIndex;
	}
	return INDEX_NONE;
}


FSlateAttributeDescriptor::FAttribute const* FSlateAttributeDescriptor::FindMemberAttribute(OffsetType AttributeOffset) const
{
	FSlateAttributeDescriptor::FAttribute const* Result = Attributes.FindByPredicate([AttributeOffset](const FAttribute& Other) { return Other.Offset == AttributeOffset; });
	check(Result == nullptr || Result->bIsMemberAttribute);
	return Result;
}


FSlateAttributeDescriptor::FAttribute* FSlateAttributeDescriptor::FindAttribute(FName AttributeName)
{
	return Attributes.FindByPredicate([AttributeName](const FAttribute& Other) { return Other.Name == AttributeName; });
}


FSlateAttributeDescriptor::FInitializer::FAttributeEntry FSlateAttributeDescriptor::AddMemberAttribute(FName AttributeName, OffsetType Offset, FInvalidateWidgetReasonAttribute Reason)
{
	check(!AttributeName.IsNone());

	int32 NewIndex = INDEX_NONE;
	FAttribute const* FoundAttribute = FindAttribute(AttributeName);
	if (ensureAlwaysMsgf(FoundAttribute == nullptr, TEXT("The attribute '%s' already exist. (Do you have the correct parrent class in SLATE_DECLARE_WIDGET)"), *AttributeName.ToString()))
	{
		NewIndex = Attributes.AddZeroed();
		FAttribute& NewAttribute = Attributes[NewIndex];
		NewAttribute.Name = AttributeName;
		NewAttribute.Offset = Offset;
		NewAttribute.InvalidationReason = MoveTemp(Reason);
		NewAttribute.bIsMemberAttribute = true;
	}
	return FInitializer::FAttributeEntry(*this, NewIndex);
}


void FSlateAttributeDescriptor::OverrideInvalidationReason(FName AttributeName, FInvalidateWidgetReasonAttribute Reason)
{
	check(!AttributeName.IsNone());

	FAttribute* FoundAttribute = FindAttribute(AttributeName);
	if (ensureAlwaysMsgf(FoundAttribute != nullptr, TEXT("The attribute 's' doesn't exist."), *AttributeName.ToString()))
	{
		FoundAttribute->InvalidationReason = MoveTemp(Reason);
	}
}


void FSlateAttributeDescriptor::SetPrerequisite(FSlateAttributeDescriptor::FAttribute& Attribute, FName Prerequisite, bool bSetAsDependency)
{
	if (Prerequisite.IsNone())
	{
		Attribute.Prerequisite = FName();
		Attribute.bIsPrerequisiteAlsoADependency = false;
	}
	else
	{
		FAttribute const* FoundPrerequisite = FindAttribute(Prerequisite);
		if (ensureAlwaysMsgf(FoundPrerequisite, TEXT("The prerequisite '%s' doesn't exist for attribute '%s'"), *Prerequisite.ToString(), *Attribute.Name.ToString()))
		{
			Attribute.Prerequisite = Prerequisite;
			Attribute.bIsPrerequisiteAlsoADependency = bSetAsDependency;

			// Verify recursion
			{
				TArray<FName, TInlineAllocator<16>> Recursion;
				Recursion.Reserve(Attributes.Num());
				FAttribute const* CurrentAttribute = &Attribute;
				while (!CurrentAttribute->Prerequisite.IsNone())
				{
					if (Recursion.Contains(CurrentAttribute->Name))
					{
						ensureAlwaysMsgf(false, TEXT("The prerequsite '%s' would introduce an infinit loop with attribute '%s'."), *Prerequisite.ToString(), *Attribute.Name.ToString());
						Attribute.Prerequisite = FName();
						Attribute.bIsPrerequisiteAlsoADependency = false;
						break;
					}
					Recursion.Add(CurrentAttribute->Name);
					CurrentAttribute = FindAttribute(CurrentAttribute->Prerequisite);
					check(CurrentAttribute);
				}
			}
		}
		else
		{
			Attribute.Prerequisite = FName();
			Attribute.bIsPrerequisiteAlsoADependency = false;
		}
	}
}


void FSlateAttributeDescriptor::SetUpdateWhenCollapsed(FAttribute& Attribute, bool bUpdate)
{
	Attribute.bUpdateWhenCollapsed = bUpdate;
}