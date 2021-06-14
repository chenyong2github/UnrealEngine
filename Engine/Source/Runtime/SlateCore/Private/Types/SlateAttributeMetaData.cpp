// Copyright Epic Games, Inc. All Rights Reserved.

#include "Types/SlateAttributeMetaData.h"

#include "Algo/BinarySearch.h"
#include "Layout/Children.h"
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
		const TSharedRef<ISlateMetaData>& SlateMetaData = OwningWidget.MetaData[0];
		check(SlateMetaData->IsOfType<FSlateAttributeMetaData>());
		return &(static_cast<FSlateAttributeMetaData&>(SlateMetaData.Get()));
	}
#if WITH_SLATE_DEBUGGING
	else if (OwningWidget.MetaData.Num() > 0)
	{
		const TSharedRef<ISlateMetaData>& SlateMetaData = OwningWidget.MetaData[0];
		if (SlateMetaData->IsOfType<FSlateAttributeMetaData>())
		{
			ensureMsgf(false, TEXT("bHasRegisteredSlateAttribute should be set on the SWidget '%s'"), *FReflectionMetaData::GetWidgetDebugInfo(OwningWidget));
			return &(static_cast<FSlateAttributeMetaData&>(SlateMetaData.Get()));
		}
	}
#endif
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
		if (OwningWidget.IsConstructed() && OwningWidget.IsAttributesUpdatesEnabled())
		{
			OwningWidget.Invalidate(EInvalidateWidgetReason::AttributeRegistration);
		}
	}
}


void FSlateAttributeMetaData::RegisterAttributeImpl(SWidget& OwningWidget, FSlateAttributeBase& Attribute, ESlateAttributeType AttributeType, TUniquePtr<ISlateAttributeGetter>&& Getter)
{
	const int32 FoundIndex = IndexOfAttribute(Attribute);
	if (FoundIndex != INDEX_NONE)
	{
		Attributes[FoundIndex].Getter = MoveTemp(Getter);
	}
	else
	{
		if (AttributeType == ESlateAttributeType::Member)
		{
			// MemberAttribute are optional for now but will be needed in the future
			const FSlateAttributeDescriptor::OffsetType Offset = Private::FindOffet(OwningWidget, Attribute);
			const FSlateAttributeDescriptor::FAttribute* FoundMemberAttribute = OwningWidget.GetWidgetClass().GetAttributeDescriptor().FindMemberAttribute(Offset);
			if (FoundMemberAttribute)
			{
				const int32 InsertLocation = Algo::LowerBoundBy(Attributes, FoundMemberAttribute->GetSortOrder(), [](const FGetterItem& Item) { return Item.SortOrder; }, TLess<>());
				FGetterItem& GetterItem = Attributes.Insert_GetRef({ &Attribute, FoundMemberAttribute->GetSortOrder(), MoveTemp(Getter), FoundMemberAttribute }, InsertLocation);
				GetterItem.AttributeType = ESlateAttributeType::Member;
				if (FoundMemberAttribute->DoesAffectVisibility())
				{
					++AffectVisibilityCounter;
				}
			}
			else
			{
				const uint32 SortOrder = FSlateAttributeDescriptor::DefaultSortOrder(Offset);

				const  int32 InsertLocation = Algo::LowerBoundBy(Attributes, SortOrder, [](const FGetterItem& Item) { return Item.SortOrder; }, TLess<>());
				FGetterItem& GetterItem = Attributes.Insert_GetRef({&Attribute, SortOrder, MoveTemp(Getter)}, InsertLocation);
				GetterItem.AttributeType = ESlateAttributeType::Member;
			}
		}
		else if (AttributeType == ESlateAttributeType::Managed)
		{
			const uint32 ManagedSortOrder = std::numeric_limits<uint32>::max();
			FGetterItem& GetterItem = Attributes.Emplace_GetRef(&Attribute, ManagedSortOrder, MoveTemp(Getter));
			GetterItem.AttributeType = ESlateAttributeType::Managed;
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
			if (OwningWidget.IsConstructed() && OwningWidget.IsAttributesUpdatesEnabled())
			{
				OwningWidget.Invalidate(EInvalidateWidgetReason::AttributeRegistration);
			}
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
		if (Attributes[FoundIndex].CachedAttributeDescriptor && Attributes[FoundIndex].CachedAttributeDescriptor->DoesAffectVisibility())
		{
			check(AffectVisibilityCounter > 0);
			--AffectVisibilityCounter;
		}
		Attributes.RemoveAt(FoundIndex); // keep the order valid
		return true;
	}
	return false;
}


TArray<FName> FSlateAttributeMetaData::GetAttributeNames(const SWidget& OwningWidget)
{
	TArray<FName> Names;
	if (FSlateAttributeMetaData* AttributeMetaData = FSlateAttributeMetaData::FindMetaData(OwningWidget))
	{
		Names.Reserve(AttributeMetaData->Attributes.Num());
		for (const FGetterItem& Getter : AttributeMetaData->Attributes)
		{
			const FName Name = Getter.GetAttributeName(OwningWidget);
			if (Name.IsValid())
			{
				Names.Add(Name);
			}
		}
	}
	return Names;
}


FName FSlateAttributeMetaData::FGetterItem::GetAttributeName(const SWidget& OwningWidget) const
{
	return CachedAttributeDescriptor ? CachedAttributeDescriptor->GetName() : FName();
}


void FSlateAttributeMetaData::InvalidateWidget(SWidget& OwningWidget, const FSlateAttributeBase& Attribute, ESlateAttributeType AttributeType, EInvalidateWidgetReason Reason)
{
	// The widget is in the construction phase or is building in the WidgetList.
	//It's already invalidated... no need to keep invalidating it.
	//N.B. no needs to set the bUpatedManually in this case because
	//	1. they are in construction, so they will all be called anyway
	//	2. they are in WidgetList, so the SlateAttribute.Set will not be called
	if (!OwningWidget.IsConstructed())
	{
		return;
	}

	auto InvalidateForMember = [Reason](SWidget& OwningWidget, const FSlateAttributeBase& Attribute) -> EInvalidateWidgetReason
	{
		const FSlateAttributeDescriptor::OffsetType Offset = Private::FindOffet(OwningWidget, Attribute);
		if (const FSlateAttributeDescriptor::FAttribute* FoundAttribute = OwningWidget.GetWidgetClass().GetAttributeDescriptor().FindMemberAttribute(Offset))
		{
			ensureMsgf(Reason == EInvalidateWidgetReason::None, TEXT("SWidget is using the AttributeDescriptor, the invalidation reason should only be defined in one place. See SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION"));
			FoundAttribute->ExecuteOnValueChangedIfBound(OwningWidget);
			return FoundAttribute->GetInvalidationReason(OwningWidget);
		}
		return Reason;
	};

	const FSlateAttributeDescriptor::FAttributeValueChangedDelegate* OnValueChangedCallback = nullptr;

	if (FSlateAttributeMetaData* AttributeMetaData = FSlateAttributeMetaData::FindMetaData(OwningWidget))
	{
		const int32 FoundIndex = AttributeMetaData->IndexOfAttribute(Attribute);
		if (FoundIndex != INDEX_NONE)
		{
			FGetterItem& GetterItem = AttributeMetaData->Attributes[FoundIndex];
			if (GetterItem.CachedAttributeDescriptor)
			{
				ensureMsgf(Reason == EInvalidateWidgetReason::None, TEXT("SWidget is using the AttributeDescriptor, the invalidation reason should only be defined in one place. See SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION"));
				GetterItem.CachedAttributeDescriptor->ExecuteOnValueChangedIfBound(OwningWidget);
				Reason = GetterItem.CachedAttributeDescriptor->GetInvalidationReason(OwningWidget);
			}
		}
		// Not registered/bound but may be defined in the Descriptor
		else if (AttributeType == ESlateAttributeType::Member)
		{
			Reason = InvalidateForMember(OwningWidget, Attribute);
		}

		Reason |= AttributeMetaData->CachedInvalidationReason;
		AttributeMetaData->CachedInvalidationReason = EInvalidateWidgetReason::None;
	}
	else if (AttributeType == ESlateAttributeType::Member)
	{
		Reason = InvalidateForMember(OwningWidget, Attribute);
	}

#if WITH_SLATE_DEBUGGING
	ensureAlwaysMsgf(FSlateAttributeBase::IsInvalidateWidgetReasonSupported(Reason), TEXT("%s is not an EInvalidateWidgetReason supported by SlateAttribute."), *LexToString(Reason));
#endif

	OwningWidget.Invalidate(Reason);
}


void FSlateAttributeMetaData::UpdateAllAttributes(SWidget& OwningWidget, EInvalidationPermission InvalidationStyle)
{
	if (FSlateAttributeMetaData* AttributeMetaData = FSlateAttributeMetaData::FindMetaData(OwningWidget))
	{
		AttributeMetaData->UpdateAttributesImpl(OwningWidget, InvalidationStyle, 0, AttributeMetaData->Attributes.Num());
	}
}


void FSlateAttributeMetaData::UpdateOnlyVisibilityAttributes(SWidget& OwningWidget, EInvalidationPermission InvalidationStyle)
{
	if (FSlateAttributeMetaData* AttributeMetaData = FSlateAttributeMetaData::FindMetaData(OwningWidget))
	{
		if (AttributeMetaData->AffectVisibilityCounter > 0)
		{
			const int32 StartIndex = 0;
			const int32 EndIndex = AttributeMetaData->AffectVisibilityCounter;
			AttributeMetaData->UpdateAttributesImpl(OwningWidget, InvalidationStyle, StartIndex, EndIndex);
		}
	}
}


void FSlateAttributeMetaData::UpdateExceptVisibilityAttributes(SWidget& OwningWidget, EInvalidationPermission InvalidationStyle)
{
	if (FSlateAttributeMetaData* AttributeMetaData = FSlateAttributeMetaData::FindMetaData(OwningWidget))
	{
		if (AttributeMetaData->AffectVisibilityCounter < AttributeMetaData->Attributes.Num())
		{
			const int32 StartIndex = AttributeMetaData->AffectVisibilityCounter;
			const int32 EndIndex = AttributeMetaData->Attributes.Num();
			AttributeMetaData->UpdateAttributesImpl(OwningWidget, InvalidationStyle, StartIndex, EndIndex);
		}
	}
}


void FSlateAttributeMetaData::UpdateChildrenOnlyVisibilityAttributes(SWidget& OwningWidget, EInvalidationPermission InvalidationStyle, bool bRecursive)
{
	OwningWidget.GetChildren()->ForEachWidget([InvalidationStyle, bRecursive](SWidget& Child)
		{
			UpdateOnlyVisibilityAttributes(Child, InvalidationStyle);
			if (bRecursive)
			{
				UpdateChildrenOnlyVisibilityAttributes(Child, InvalidationStyle, bRecursive);
			}
		});
}


void FSlateAttributeMetaData::UpdateAttributesImpl(SWidget& OwningWidget, EInvalidationPermission InvalidationStyle, int32 StartIndex, int32 IndexNum)
{
	bool bInvalidateIfNeeded = (InvalidationStyle == EInvalidationPermission::AllowInvalidation) || (InvalidationStyle == EInvalidationPermission::AllowInvalidationIfConstructed && OwningWidget.IsConstructed());
	bool bAllowInvalidation = bInvalidateIfNeeded || InvalidationStyle == EInvalidationPermission::DelayInvalidation;
	EInvalidateWidgetReason InvalidationReason = EInvalidateWidgetReason::None;
	for (int32 Index = StartIndex; Index < IndexNum; ++Index)
	{
		FGetterItem& GetterItem = Attributes[Index];

		ISlateAttributeGetter::FUpdateAttributeResult Result = GetterItem.Getter->UpdateAttribute(OwningWidget);
		if (Result.bInvalidationRequested)
		{
			if (GetterItem.CachedAttributeDescriptor)
			{
				GetterItem.CachedAttributeDescriptor->ExecuteOnValueChangedIfBound(OwningWidget);
				if (bAllowInvalidation)
				{
					InvalidationReason |= GetterItem.CachedAttributeDescriptor->GetInvalidationReason(OwningWidget);
				}
			}
			else if (bAllowInvalidation)
			{
				InvalidationReason |= Result.InvalidationReason;
			}
		}
	}

#if WITH_SLATE_DEBUGGING
	ensureAlwaysMsgf(FSlateAttributeBase::IsInvalidateWidgetReasonSupported(InvalidationReason), TEXT("'%s' is not an EInvalidateWidgetReason supported by SlateAttribute."), *LexToString(InvalidationReason));
#endif

	if (bInvalidateIfNeeded)
	{
		OwningWidget.Invalidate(InvalidationReason | CachedInvalidationReason);
		CachedInvalidationReason = EInvalidateWidgetReason::None;
	}
	else if (InvalidationStyle == EInvalidationPermission::DelayInvalidation)
	{
		CachedInvalidationReason |= InvalidationReason;
	}
	else if (InvalidationStyle == EInvalidationPermission::DenyAndClearDelayedInvalidation)
	{
		CachedInvalidationReason = EInvalidateWidgetReason::None;
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
			check(GetterItem.Getter.Get());
			ISlateAttributeGetter::FUpdateAttributeResult Result = GetterItem.Getter->UpdateAttribute(OwningWidget);
			if (Result.bInvalidationRequested && OwningWidget.IsConstructed())
			{
				EInvalidateWidgetReason InvalidationReason = Result.InvalidationReason;
				if (GetterItem.CachedAttributeDescriptor)
				{
					GetterItem.CachedAttributeDescriptor->ExecuteOnValueChangedIfBound(OwningWidget);
					InvalidationReason = GetterItem.CachedAttributeDescriptor->GetInvalidationReason(OwningWidget);
				}

#if WITH_SLATE_DEBUGGING
				ensureAlwaysMsgf(FSlateAttributeBase::IsInvalidateWidgetReasonSupported(InvalidationReason), TEXT("%s is not an EInvalidateWidgetReason supported by SlateAttribute."), *LexToString(InvalidationReason));
#endif
				OwningWidget.Invalidate(InvalidationReason | AttributeMetaData->CachedInvalidationReason);
				AttributeMetaData->CachedInvalidationReason = EInvalidateWidgetReason::None;
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
