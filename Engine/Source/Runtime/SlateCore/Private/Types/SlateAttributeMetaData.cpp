// Copyright Epic Games, Inc. All Rights Reserved.

#include "Types/SlateAttributeMetaData.h"

#include "Algo/BinarySearch.h"
#include "Types/ReflectionMetadata.h"
#include "Widgets/SWidget.h"

#include <limits>


namespace Private
{
	FSlateAttributeDescriptor::OffsetType FindOffet(const SWidget& OwningWidget, const FSlateAttributeBase& Attribute)
	{
		UPTRINT Offset = (UPTRINT)(&Attribute) - (UPTRINT)(&OwningWidget);
		ensure(Offset <= std::numeric_limits<FSlateAttributeDescriptor::OffsetType>::max());
		return (FSlateAttributeDescriptor::OffsetType)(Offset);
	}
}


FSlateAttributeMetaData* FSlateAttributeMetaData::FindMetaData(const SWidget& OwningWidget)
{
	if (OwningWidget.HasRegisteredSlateAttribute())
	{
		check(OwningWidget.MetaData.Num() > 0);
		TSharedRef<ISlateMetaData> SlateMetaData = OwningWidget.MetaData[0];
		check(SlateMetaData->IsOfType<FSlateAttributeMetaData>());
		return &(StaticCastSharedRef<FSlateAttributeMetaData>(SlateMetaData).Get());
	}
	else if (OwningWidget.MetaData.Num() > 0)
	{
		TSharedRef<ISlateMetaData> SlateMetaData = OwningWidget.MetaData[0];
		if (SlateMetaData->IsOfType<FSlateAttributeMetaData>())
		{
			return &(StaticCastSharedRef<FSlateAttributeMetaData>(SlateMetaData).Get());
		}
	}
	return nullptr;
}


void FSlateAttributeMetaData::RegisterAttribute(SWidget& OwningWidget, FSlateAttributeBase& Attribute, ESlateAttributeType AttributeType, TUniquePtr<ISlateAttributeGetter>&& Wrapper)
{
	if (FSlateAttributeMetaData* AttributeMetaData = FSlateAttributeMetaData::FindMetaData(OwningWidget))
	{
		AttributeMetaData->RegisterAttributeImpl(OwningWidget, Attribute, AttributeType, MoveTemp(Wrapper));
	}
	else
	{
		TSharedRef<FSlateAttributeMetaData> NewAttributeMetaData = MakeShared<FSlateAttributeMetaData>();
		NewAttributeMetaData->RegisterAttributeImpl(OwningWidget, Attribute, AttributeType, MoveTemp(Wrapper));
		OwningWidget.bHasRegisteredSlateAttribute = true;
		OwningWidget.MetaData.Insert(NewAttributeMetaData, 0);
		OwningWidget.Invalidate(EInvalidateWidgetReason::AttributeRegistration);
	}
}


void FSlateAttributeMetaData::RegisterAttributeImpl(SWidget& OwningWidget, FSlateAttributeBase& Attribute, ESlateAttributeType AttributeType, TUniquePtr<ISlateAttributeGetter>&& Getter)
{
	const int32 FoundIndex = IndexOfAttribute(Attribute);
	if (FoundIndex != INDEX_NONE)
	{
		Attributes[FoundIndex].Getter = MoveTemp(Getter);
		Attributes[FoundIndex].bUpdatedOnce = false;
	}
	else
	{
		if (AttributeType == ESlateAttributeType::Member)
		{
			// MemberAttribute are optional for now but will be needed in the future
			const FSlateAttributeDescriptor::OffsetType Offset = Private::FindOffet(OwningWidget, Attribute);
			const FSlateAttributeDescriptor& Descriptor = OwningWidget.GetWidgetClass().GetAttributeDescriptor();
			const int32 FoundMemberAttributeIndex = Descriptor.IndexOfMemberAttribute(Offset);

			if (FoundMemberAttributeIndex != INDEX_NONE)
			{
				FSlateAttributeDescriptor::FAttribute const& FoundAttribute = Descriptor.GetAttributeAtIndex(FoundMemberAttributeIndex);
				check(FoundMemberAttributeIndex < std::numeric_limits<int16>::max());

				const int32 InsertLocation = Algo::LowerBoundBy(Attributes, FoundAttribute.SortOrder, [](const FGetterItem& Item) { return Item.SortOrder; }, TLess<>());

				FGetterItem& GetterItem = Attributes.Insert_GetRef({ &Attribute, FoundAttribute.SortOrder, MoveTemp(Getter), (int16)FoundMemberAttributeIndex }, InsertLocation);
				GetterItem.bIsMemberType = true;

				// Do I have dependency or am I a dependency
				if (!FoundAttribute.Prerequisite.IsNone() && FoundAttribute.bIsPrerequisiteAlsoADependency)
				{
					// I can only be updated if the prerequisite is updated
					const int32 FoundDependencyAttributeIndex = Descriptor.IndexOfMemberAttribute(FoundAttribute.Prerequisite);
					check(FoundDependencyAttributeIndex < std::numeric_limits<int16>::max());
					GetterItem.CachedAttributeDependencyIndex = (int16)FoundDependencyAttributeIndex;
				}
				GetterItem.bIsADependencyForSomeoneElse = FoundAttribute.bIsADependencyForSomeoneElse;
				GetterItem.bUpdateWhenCollapsed = FoundAttribute.bUpdateWhenCollapsed;
				if (GetterItem.bUpdateWhenCollapsed)
				{
					++CollaspedAttributeCounter;
				}
			}
			else
			{
				const uint32 SortOrder = FSlateAttributeDescriptor::DefaultSortOrder(Offset);

				const  int32 InsertLocation = Algo::LowerBoundBy(Attributes, SortOrder, [](const FGetterItem& Item) { return Item.SortOrder; }, TLess<>());
				FGetterItem& GetterItem = Attributes.Insert_GetRef({&Attribute, SortOrder, MoveTemp(Getter)}, InsertLocation);
				GetterItem.bIsMemberType = true;
			}
		}
		else if (AttributeType == ESlateAttributeType::Managed)
		{
			const uint32 ManagedSortOrder = std::numeric_limits<uint32>::max();
			FGetterItem& GetterItem = Attributes.Emplace_GetRef(&Attribute, ManagedSortOrder, MoveTemp(Getter));
			GetterItem.bIsManagedType = true;
		}
		else
		{
			ensureMsgf(false, TEXT("The SlateAttributeType is not supported"));
		}
	}
}


bool FSlateAttributeMetaData::UnregisterAttribute(SWidget& OwningWidget, const FSlateAttributeBase& Attribute)
{
	if (FSlateAttributeMetaData* AttributeMetaData = FSlateAttributeMetaData::FindMetaData(OwningWidget))
	{
		const bool bResult = AttributeMetaData->UnregisterAttributeImpl(Attribute);
		if (AttributeMetaData->Attributes.Num() == 0)
		{
			check(bResult); // if the num is 0 then we should have remove an item.
			OwningWidget.bHasRegisteredSlateAttribute = false;
			OwningWidget.MetaData.RemoveAtSwap(0);
			OwningWidget.Invalidate(EInvalidateWidgetReason::AttributeRegistration);
		}
		return bResult;
	}
	return false;
}


bool FSlateAttributeMetaData::UnregisterAttributeImpl(const FSlateAttributeBase& Attribute)
{
	const int32 FoundIndex = IndexOfAttribute(Attribute);
	if (FoundIndex != INDEX_NONE)
	{
		if (Attributes[FoundIndex].bUpdateWhenCollapsed)
		{
			check(CollaspedAttributeCounter > 0);
			--CollaspedAttributeCounter;
		}
		Attributes.RemoveAt(FoundIndex); // keep the order valid
		return true;
	}
	return false;
}


EInvalidateWidgetReason FSlateAttributeMetaData::FGetterItem::GetInvalidationReason(const SWidget& OwningWidget, EInvalidateWidgetReason Reason) const
{
	if (CachedAttributeDescriptorIndex != INDEX_NONE)
	{
		return OwningWidget.GetWidgetClass().GetAttributeDescriptor().GetAttributeAtIndex(CachedAttributeDescriptorIndex).InvalidationReason.Get(OwningWidget);
	}
	return Reason;
}


void FSlateAttributeMetaData::InvalidateWidget(SWidget& OwningWidget, const FSlateAttributeBase& Attribute, ESlateAttributeType AttributeType, EInvalidateWidgetReason Reason)
{
	// The widget is in the construction phase or is building in the WidgetList.
	//It's already invalidated... no need to keep invalidating it.
	//N.B. no needs to set the bUpatedManually in this case because
	//	1. they are in construction, so they will all be called anyway
	//	2. they are in WidgetList, so the SlateAttribute.Set will not be called
	if (OwningWidget.bPauseAttributeInvalidation)
	{
		return;
	}

	if (FSlateAttributeMetaData* AttributeMetaData = FSlateAttributeMetaData::FindMetaData(OwningWidget))
	{
		const int32 FoundIndex = AttributeMetaData->IndexOfAttribute(Attribute);
		if (FoundIndex != INDEX_NONE)
		{
			FGetterItem& GetterItem = AttributeMetaData->Attributes[FoundIndex];
			Reason = GetterItem.GetInvalidationReason(OwningWidget, Reason);

			// The dependency attribute need to be updated in the update loop (note that it may not be registered yet)
			if (GetterItem.bIsADependencyForSomeoneElse)
			{
				GetterItem.bUpatedManually = true;
				AttributeMetaData->bHasUpdatedManuallyFlagToReset = true;
			}
		}
		// Not registered but may be defined in the Descriptor
		else if (AttributeType == ESlateAttributeType::Member)
		{
			FSlateAttributeDescriptor const& AttributeDescriptor = OwningWidget.GetWidgetClass().GetAttributeDescriptor();
			const FSlateAttributeDescriptor::OffsetType Offset = Private::FindOffet(OwningWidget, Attribute);
			if (FSlateAttributeDescriptor::FAttribute const* FoundAttribute = AttributeDescriptor.FindMemberAttribute(Offset))
			{
				Reason = FoundAttribute->InvalidationReason.Get(OwningWidget);

				if (FoundAttribute->bIsADependencyForSomeoneElse)
				{
					// Find if that dependency is registered, if not it is ok because every attribute is updated at least once
					// Set UpdatedOnce to false to force a new update.
					AttributeDescriptor.ForEachDependency(*FoundAttribute, [AttributeMetaData](int32 DependencyIndex)
					{
						FGetterItem* FoundOther = AttributeMetaData->Attributes.FindByPredicate([DependencyIndex](FGetterItem const& Other)
						{
							return Other.CachedAttributeDescriptorIndex == DependencyIndex;
						});
						if (FoundOther)
						{
							FoundOther->bUpdatedOnce = false;
						}
					});
				}
			}
		}
	}
	else if (AttributeType == ESlateAttributeType::Member)
	{
		const FSlateAttributeDescriptor::OffsetType Offset = Private::FindOffet(OwningWidget, Attribute);
		if (FSlateAttributeDescriptor::FAttribute const* FoundAttribute = OwningWidget.GetWidgetClass().GetAttributeDescriptor().FindMemberAttribute(Offset))
		{
			Reason = FoundAttribute->InvalidationReason.Get(OwningWidget);
		}
	}

	OwningWidget.Invalidate(Reason);
}


void FSlateAttributeMetaData::UpdateAttributes(SWidget& OwningWidget)
{
	if (FSlateAttributeMetaData* AttributeMetaData = FSlateAttributeMetaData::FindMetaData(OwningWidget))
	{
		AttributeMetaData->UpdateAttributes(OwningWidget, false);
	}
}


void FSlateAttributeMetaData::UpdateCollapsedAttributes(SWidget& OwningWidget)
{
	if (FSlateAttributeMetaData* AttributeMetaData = FSlateAttributeMetaData::FindMetaData(OwningWidget))
	{
		// Does it have any collapsed attributes
		if (AttributeMetaData->CollaspedAttributeCounter > 0)
		{
			AttributeMetaData->UpdateAttributes(OwningWidget, true);
		}
	}
}


void FSlateAttributeMetaData::UpdateAttributes(SWidget& OwningWidget, bool bOnlyCollapsed)
{
	EInvalidateWidgetReason InvalidationReason = EInvalidateWidgetReason::None;
	for (int32 Index = 0; Index < Attributes.Num(); ++Index)
	{
		FGetterItem& GetterItem = Attributes[Index];

		// Only update attributes that needs to be updated when the widget is collapsed
		if (bOnlyCollapsed && !GetterItem.bUpdateWhenCollapsed)
		{
			continue;
		}

		// Update every attribute at least once.
		//Check if it has a dependency and if it was updated this frame (it could be from an UpdateNow)
		if (GetterItem.CachedAttributeDependencyIndex != INDEX_NONE && GetterItem.bUpdatedOnce)
		{
			// Note that the dependency is maybe not registered and the attribute may have been invalidated manually

			// Because the list is sorted, the dependency needs to be before this element.
			bool bShouldUpdate = false;
			bool bFound = false;
			for (int32 OtherIndex = Index-1; OtherIndex >= 0; --OtherIndex)
			{
				const FGetterItem& OtherGetterItem = Attributes[OtherIndex];
				if (OtherGetterItem.CachedAttributeDescriptorIndex == GetterItem.CachedAttributeDependencyIndex)
				{
					bFound = true;
					bShouldUpdate = OtherGetterItem.bUpdatedThisFrame || OtherGetterItem.bUpatedManually;
					break;
				}
			}

			if (!bShouldUpdate)
			{
				continue;
			}
		}

		ISlateAttributeGetter::FUpdateAttributeResult Result = GetterItem.Getter->UpdateAttribute(OwningWidget);
		GetterItem.bUpdatedOnce = true;
		GetterItem.bUpdatedThisFrame = Result.bInvalidationRequested;
		if (Result.bInvalidationRequested && !OwningWidget.bPauseAttributeInvalidation)
		{
			InvalidationReason |= GetterItem.GetInvalidationReason(OwningWidget, Result.InvalidationReason);
		}
	}

	if (bHasUpdatedManuallyFlagToReset)
	{
		for (FGetterItem& GetterItem : Attributes)
		{
			GetterItem.bUpatedManually = false;
		}
	}
	bHasUpdatedManuallyFlagToReset = false;

	if (!OwningWidget.bPauseAttributeInvalidation)
	{
		OwningWidget.Invalidate(InvalidationReason);
	}
}


void FSlateAttributeMetaData::UpdateAttribute(SWidget& OwningWidget, FSlateAttributeBase& Attribute)
{
	if (FSlateAttributeMetaData* AttributeMetaData = FSlateAttributeMetaData::FindMetaData(OwningWidget))
	{
		const int32 FoundIndex = AttributeMetaData->IndexOfAttribute(Attribute);
		if (FoundIndex != INDEX_NONE)
		{
			FGetterItem& GetterItem = AttributeMetaData->Attributes[FoundIndex];
			GetterItem.bUpdatedOnce = true;
			check(GetterItem.Getter.Get());
			ISlateAttributeGetter::FUpdateAttributeResult Result = GetterItem.Getter->UpdateAttribute(OwningWidget);
			if (Result.bInvalidationRequested)
			{
				if (!OwningWidget.bPauseAttributeInvalidation)
				{
					OwningWidget.Invalidate(GetterItem.GetInvalidationReason(OwningWidget, Result.InvalidationReason));
				}

				// The dependency attribute need to be updated in the update loop (note that it may not be registered yet)
				if (GetterItem.bIsADependencyForSomeoneElse)
				{
					GetterItem.bUpatedManually = true;
					AttributeMetaData->bHasUpdatedManuallyFlagToReset = true;
				}
			}
		}
	}
}


bool FSlateAttributeMetaData::IsAttributeBound(const SWidget& OwningWidget, const FSlateAttributeBase& Attribute)
{
	if (FSlateAttributeMetaData* AttributeMetaData = FSlateAttributeMetaData::FindMetaData(OwningWidget))
	{
		return AttributeMetaData->IndexOfAttribute(Attribute) != INDEX_NONE;
	}
	return false;
}


SlateAttributePrivate::ISlateAttributeGetter* FSlateAttributeMetaData::GetAttributeGetter(const SWidget& OwningWidget, const FSlateAttributeBase& Attribute)
{
	if (FSlateAttributeMetaData* AttributeMetaData = FSlateAttributeMetaData::FindMetaData(OwningWidget))
	{
		const int32 FoundIndex = AttributeMetaData->IndexOfAttribute(Attribute);
		if (FoundIndex != INDEX_NONE)
		{
			return AttributeMetaData->Attributes[FoundIndex].Getter.Get();
		}
	}
	return nullptr;
}


FDelegateHandle FSlateAttributeMetaData::GetAttributeGetterHandle(const SWidget& OwningWidget, const FSlateAttributeBase& Attribute)
{
	if (FSlateAttributeMetaData* AttributeMetaData = FSlateAttributeMetaData::FindMetaData(OwningWidget))
	{
		const int32 FoundIndex = AttributeMetaData->IndexOfAttribute(Attribute);
		if (FoundIndex != INDEX_NONE)
		{
			return AttributeMetaData->Attributes[FoundIndex].Getter->GetDelegateHandle();
		}
	}
	return FDelegateHandle();
}


void FSlateAttributeMetaData::MoveAttribute(const SWidget& OwningWidget, FSlateAttributeBase& NewAttribute, ESlateAttributeType AttributeType, const FSlateAttributeBase* PreviousAttribute)
{
	checkf(AttributeType == ESlateAttributeType::Managed, TEXT("TSlateAttribute cannot be moved. This should be already prevented in SlateAttribute.h"));
	if (FSlateAttributeMetaData* AttributeMetaData = FSlateAttributeMetaData::FindMetaData(OwningWidget))
	{
		const int32 FoundIndex = AttributeMetaData->Attributes.IndexOfByPredicate([PreviousAttribute](const FGetterItem& Item) { return Item.Attribute == PreviousAttribute; });
		if (FoundIndex != INDEX_NONE)
		{
			AttributeMetaData->Attributes[FoundIndex].Attribute = &NewAttribute;
			AttributeMetaData->Attributes[FoundIndex].Getter->SetAttribute(NewAttribute);
			//Attributes.Sort(); // Managed are always at the end and there order is not realiable.
		}
	}
}
