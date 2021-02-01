// Copyright Epic Games, Inc. All Rights Reserved.

#include "Types/SlateAttributeDescriptor.h"
#include "Types/SlateAttribute.h"
#include "Misc/MemStack.h"


FSlateAttributeDescriptor::FInitializer::FAttributeEntry::FAttributeEntry(FSlateAttributeDescriptor& InDescriptor, int32 InAttributeIndex)
	: Descriptor(InDescriptor)
	, AttributeIndex(InAttributeIndex)
{}
			
FSlateAttributeDescriptor::FInitializer::FAttributeEntry& FSlateAttributeDescriptor::FInitializer::FAttributeEntry::SetPrerequisite(FName Prerequisite)
{
	if (Descriptor.Attributes.IsValidIndex(AttributeIndex))
	{
		Descriptor.SetPrerequisite(Descriptor.Attributes[AttributeIndex], Prerequisite);
	}
	return *this;
}

FSlateAttributeDescriptor::FInitializer::FAttributeEntry& FSlateAttributeDescriptor::FInitializer::FAttributeEntry::UpdateEveryFrame()
{
	if (Descriptor.Attributes.IsValidIndex(AttributeIndex))
	{
		Descriptor.Attributes[AttributeIndex].Dependency = FName();
	}
	return *this;
}

FSlateAttributeDescriptor::FInitializer::FAttributeEntry& FSlateAttributeDescriptor::FInitializer::FAttributeEntry::UpdateDependency(FName Dependency)
{
	if (Descriptor.Attributes.IsValidIndex(AttributeIndex))
	{
		Descriptor.SetDependency(Descriptor.Attributes[AttributeIndex], Dependency);
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
	// because adding the attribute is not required at the moment
	//try to not change the order in which they were added
	//for (int32 Index = 0; Index < Descriptor.Attributes.Num(); ++Index)
	//{
	//	if (!Descriptor.Attributes[Index].Prerequisite.IsNone())
	//	{
	//		// Find the Prerequisite index
	//		const FName Prerequisite = Descriptor.Attributes[Index].Prerequisite;
	//		const int32 FoundIndex = Descriptor.Attributes.IndexOfByPredicate([Prerequisite](const FAttribute& Other) { return Other.Name == Prerequisite; });
	//		if (ensureAlwaysMsgf(Descriptor.Attributes.IsValidIndex(FoundIndex), TEXT("The Prerequisite '%s' doesn't exist"), *Prerequisite.ToString()))
	//		{
	//			Descriptor.Attributes[Index].SortOrder = Descriptor.Attributes[FoundIndex].SortOrder + 1;
	//		}
	//	}
	//}
}


FSlateAttributeDescriptor::FInitializer::FAttributeEntry FSlateAttributeDescriptor::FInitializer::AddMemberAttribute(FName AttributeName, OffsetType Offset)
{
	return Descriptor.AddMemberAttribute(AttributeName, Offset, TAttribute<EInvalidateWidgetReason>());
}


FSlateAttributeDescriptor::FInitializer::FAttributeEntry FSlateAttributeDescriptor::FInitializer::AddMemberAttribute(FName AttributeName, OffsetType Offset, TAttribute<EInvalidateWidgetReason> Reason)
{
	check(Reason.IsSet());
	return Descriptor.AddMemberAttribute(AttributeName, Offset, MoveTemp(Reason));
}


void FSlateAttributeDescriptor::FInitializer::OverrideInvalidationReason(FName AttributeName, TAttribute<EInvalidateWidgetReason> Reason)
{
	check(Reason.IsSet());
	Descriptor.OverrideInvalidationReason(AttributeName, MoveTemp(Reason));
}


void FSlateAttributeDescriptor::FInitializer::SetPrerequisite(FName AttributeName, FName Prerequisite)
{
	check(!AttributeName.IsNone());

	FAttribute* FoundAttribute = Descriptor.FindAttribute(AttributeName);
	if (ensureAlwaysMsgf(FoundAttribute != nullptr, TEXT("The attribute '%s' doesn't exist."), *AttributeName.ToString()))
	{
		Descriptor.SetPrerequisite(*FoundAttribute, Prerequisite);
	}
}


FSlateAttributeDescriptor::FAttribute const* FSlateAttributeDescriptor::FindAttribute(FName AttributeName) const
{
	return Attributes.FindByPredicate([AttributeName](const FAttribute& Other) { return Other.Name == AttributeName; });
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


FSlateAttributeDescriptor::FInitializer::FAttributeEntry FSlateAttributeDescriptor::AddMemberAttribute(FName AttributeName, OffsetType Offset, TAttribute<EInvalidateWidgetReason>&& Reason)
{
	check(!AttributeName.IsNone());

	int32 NewIndex = INDEX_NONE;
	FAttribute const* FoundAttribute = FindAttribute(AttributeName);
	if (ensureAlwaysMsgf(FoundAttribute == nullptr, TEXT("The attribute '%s' already exist."), *AttributeName.ToString()))
	{
		NewIndex = Attributes.AddZeroed();
		FAttribute& NewAttribute = Attributes[NewIndex];
		NewAttribute.Name = AttributeName;
		NewAttribute.Offset = Offset;
		NewAttribute.InvalidationReason = MoveTemp(Reason);
		NewAttribute.SortOrder = DefaultSortOrder(Offset);
		NewAttribute.bIsMemberAttribute = true;
	}
	return FInitializer::FAttributeEntry(*this, NewIndex);
}


void FSlateAttributeDescriptor::OverrideInvalidationReason(FName AttributeName, TAttribute<EInvalidateWidgetReason>&& Reason)
{
	check(!AttributeName.IsNone());

	FAttribute* FoundAttribute = FindAttribute(AttributeName);
	if (ensureAlwaysMsgf(FoundAttribute != nullptr, TEXT("The attribute 's' doesn't exist."), *AttributeName.ToString()))
	{
		FoundAttribute->InvalidationReason = MoveTemp(Reason);
	}
}

void FSlateAttributeDescriptor::SetDependency(FSlateAttributeDescriptor::FAttribute& Attribute, FName Dependency)
{
	if (Dependency.IsNone())
	{
		Attribute.Dependency = FName();
	}
	else
	{
		FAttribute const* FoundDependency = FindAttribute(Dependency);
		if (ensureAlwaysMsgf(FoundDependency, TEXT("The Dependency '%s' doesn't exist for attribute '%s'"), *Dependency.ToString(), *Attribute.Name.ToString()))
		{
			Attribute.Dependency = Dependency;

			// Verify recursion
			{
				FMemMark Mark(FMemStack::Get());
				TArray<FName, TMemStackAllocator<>> Recursion;
				Recursion.Reserve(Attributes.Num());
				FAttribute const* CurrentAttribute = &Attribute;
				while (!CurrentAttribute->Dependency.IsNone())
				{
					if (Recursion.Contains(CurrentAttribute->Name))
					{
						ensureAlwaysMsgf(false, TEXT("The Dependency '%s' would introduce an infinit loop with attribute '%s'."), *Dependency.ToString(), *Attribute.Name.ToString());
						Attribute.Dependency = FName();
						break;
					}
					Recursion.Add(CurrentAttribute->Name);
					CurrentAttribute = FindAttribute(CurrentAttribute->Dependency);
					check(CurrentAttribute);
				}
			}
		}
		else
		{
			Attribute.Dependency = FName();
		}
	}
}

void FSlateAttributeDescriptor::SetPrerequisite(FSlateAttributeDescriptor::FAttribute& Attribute, FName Prerequisite)
{
	if (Prerequisite.IsNone())
	{
		Attribute.Prerequisite = FName();
	}
	else
	{
		FAttribute const* FoundPrerequisite = FindAttribute(Prerequisite);
		if (ensureAlwaysMsgf(FoundPrerequisite, TEXT("The prerequisite '%s' doesn't exist for attribute '%s'"), *Prerequisite.ToString(), *Attribute.Name.ToString()))
		{
			Attribute.Prerequisite = Prerequisite;
			Attribute.SortOrder = FoundPrerequisite->SortOrder + 1;

			// Verify recursion
			{
				FMemMark Mark(FMemStack::Get());
				TArray<FName, TMemStackAllocator<>> Recursion;
				Recursion.Reserve(Attributes.Num());
				FAttribute const* CurrentAttribute = &Attribute;
				while (!CurrentAttribute->Prerequisite.IsNone())
				{
					if (Recursion.Contains(CurrentAttribute->Name))
					{
						ensureAlwaysMsgf(false, TEXT("The prerequsite '%s' would introduce an infinit loop with attribute '%s'."), *Prerequisite.ToString(), *Attribute.Name.ToString());
						Attribute.Prerequisite = FName();
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
		}
	}
}
