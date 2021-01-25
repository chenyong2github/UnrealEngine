// Copyright Epic Games, Inc. All Rights Reserved.

#include "Types/SlateAttributeMetaData.h"

#include "Types/ReflectionMetadata.h"
#include "Widgets/SWidget.h"

#include <limits>


namespace Private
{
	FSlateAttributeDescriptor::OffsetType FindOffet(const SWidget& OwningWidget, FSlateAttributeBase& Attribute)
	{
		return (FSlateAttributeDescriptor::OffsetType)((UPTRINT)(&Attribute) - (UPTRINT)(&OwningWidget));
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
		if (OwningWidget.GetProxyHandle().IsValid(&OwningWidget))
		{
			//OwningWidget.FastPathProxyHandle.MarkWidgetRegisteredSlateAttributeDirty(&OwningWidget);
		}
		OwningWidget.MetaData.Insert(NewAttributeMetaData, 0);
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
			FSlateAttributeDescriptor::OffsetType Offset = Private::FindOffet(OwningWidget, Attribute);
			uint32 SortOrder = FSlateAttributeDescriptor::DefaultSortOrder(Offset);

			// MemberAttribute are optional for now but will be needed in the future
			const FSlateAttributeDescriptor& Descriptor = OwningWidget.GetWidgetClass().GetAttributeDescriptor();
			if (FSlateAttributeDescriptor::FAttribute const* FoundAttribute = Descriptor.FindMemberAttribute(Offset))
			{
				Attributes.Emplace(&Attribute, FoundAttribute->SortOrder, MoveTemp(Getter), FoundAttribute->InvalidationReason);
			}
			else
			{
				Attributes.Emplace(&Attribute, SortOrder, MoveTemp(Getter));
			}
		}
		else if (AttributeType == ESlateAttributeType::Managed)
		{
			const uint32 ManagedSortOrder = std::numeric_limits<uint32>::max();
			Attributes.Emplace(&Attribute, ManagedSortOrder, MoveTemp(Getter));
		}
		else
		{
			ensureMsgf(false, TEXT("The SlateAttributeType is not supported"));
		}

		Attributes.Sort();
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
			if (OwningWidget.GetProxyHandle().IsValid(&OwningWidget))
			{
				//OwningWidget.FastPathProxyHandle.MarkWidgetRegisteredSlateAttributeDirty(&OwningWidget);
			}
			OwningWidget.MetaData.RemoveAtSwap(0);
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
		Attributes.RemoveAt(FoundIndex);
		return true;
	}
	return false;
}


void FSlateAttributeMetaData::InvalidateWidget(SWidget& OwningWidget, const FSlateAttributeBase& Attribute, EInvalidateWidgetReason Reason)
{
	if (FSlateAttributeMetaData* AttributeMetaData = FSlateAttributeMetaData::FindMetaData(OwningWidget))
	{
		const int32 FoundIndex = AttributeMetaData->IndexOfAttribute(Attribute);
		if (FoundIndex != INDEX_NONE)
		{
			OwningWidget.Invalidate(AttributeMetaData->Attributes[FoundIndex].OverrideInvalidateReason.Get(Reason));
		}
		else
		{
			OwningWidget.Invalidate(Reason);
		}
	}
	else
	{
		OwningWidget.Invalidate(Reason);
	}
}


void FSlateAttributeMetaData::UpdateAttributes(SWidget& OwningWidget)
{
	if (FSlateAttributeMetaData* AttributeMetaData = FSlateAttributeMetaData::FindMetaData(OwningWidget))
	{
		EInvalidateWidgetReason InvalidationReason = EInvalidateWidgetReason::None;
		for (FGetterItem& GetterItem: AttributeMetaData->Attributes)
		{
			ISlateAttributeGetter::FUpdateAttributeResult Result = GetterItem.Getter->UpdateAttribute(OwningWidget);
			if (Result.bInvalidationRequested)
			{
				InvalidationReason |= GetterItem.OverrideInvalidateReason.Get(Result.InvalidationReason);
			}
		}
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
			check(GetterItem.Getter.Get());
			ISlateAttributeGetter::FUpdateAttributeResult Result = GetterItem.Getter->UpdateAttribute(OwningWidget);
			if (Result.bInvalidationRequested)
			{
				OwningWidget.Invalidate(GetterItem.OverrideInvalidateReason.Get(Result.InvalidationReason));
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
