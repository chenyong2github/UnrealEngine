// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Algo/BinarySearch.h"
#include "Types/ISlateMetaData.h"
#include "Types/SlateAttribute.h"
#include "Widgets/InvalidateWidgetReason.h"


class SWidget;

/** */
class SLATECORE_API FSlateAttributeMetaData : public ISlateMetaData
{
	friend SlateAttributePrivate::FSlateAttributeImpl;

public:
	SLATE_METADATA_TYPE(FSlateAttributeMetaData, ISlateMetaData);

	FSlateAttributeMetaData() = default;
	FSlateAttributeMetaData(const FSlateAttributeMetaData&) = delete;
	FSlateAttributeMetaData& operator=(const FSlateAttributeMetaData&) = delete;

public:
	static FSlateAttributeMetaData* FindMetaData(const SWidget& OwningWidget);
	static void UpdateAttributes(SWidget& OwningWidget);
	static void UpdateCollapsedAttributes(SWidget& OwningWidget);

public:
	bool IsBound(const FSlateAttributeBase& Attribute) const
	{
		return IndexOfAttribute(Attribute) != INDEX_NONE;
	}

	int32 RegisteredNum() const { return Attributes.Num(); }

private:
	using ESlateAttributeType = SlateAttributePrivate::ESlateAttributeType;
	using ISlateAttributeGetter = SlateAttributePrivate::ISlateAttributeGetter;
	static void RegisterAttribute(SWidget& OwningWidget, FSlateAttributeBase& Attribute, ESlateAttributeType AttributeType, TUniquePtr<ISlateAttributeGetter>&& Wrapper);
	static bool UnregisterAttribute(SWidget& OwningWidget, const FSlateAttributeBase& Attribute);
	static void InvalidateWidget(SWidget& OwningWidget, const FSlateAttributeBase& Attribute, ESlateAttributeType AttributeType, EInvalidateWidgetReason Reason);
	static void UpdateAttribute(SWidget& OwningWidget, FSlateAttributeBase& Attribute);
	static bool IsAttributeBound(const SWidget& OwningWidget, const FSlateAttributeBase& Attribute);
	static FDelegateHandle GetAttributeGetterHandle(const SWidget& OwningWidget, const FSlateAttributeBase& Attribute);
	static void MoveAttribute(const SWidget& OwningWidget, FSlateAttributeBase& NewAttribute, ESlateAttributeType AttributeType, const FSlateAttributeBase* PreviousAttribute);

private:
	void RegisterAttributeImpl(SWidget& OwningWidget, FSlateAttributeBase& Attribute, ESlateAttributeType AttributeType, TUniquePtr<ISlateAttributeGetter>&& Getter);
	bool UnregisterAttributeImpl(const FSlateAttributeBase& Attribute);
	void UpdateAttributes(SWidget& OwningWidget, bool bOnlyCollapsed);

private:
	int32 IndexOfAttribute(const FSlateAttributeBase& Attribute) const
	{
		const FSlateAttributeBase* AttributePtr = &Attribute;
		return Attributes.IndexOfByPredicate([AttributePtr](const FGetterItem& Item) { return Item.Attribute == AttributePtr; });
	}

	struct FGetterItem
	{
		FGetterItem() = default;
		FGetterItem(const FGetterItem&) = delete;
		FGetterItem(FGetterItem&&) = default;
		FGetterItem& operator=(const FGetterItem&) = delete;
		FGetterItem(FSlateAttributeBase* InAttribute, uint32 InSortOrder, TUniquePtr<SlateAttributePrivate::ISlateAttributeGetter>&& InGetter)
			: Attribute(InAttribute)
			, Getter(MoveTemp(InGetter))
			, SortOrder(InSortOrder)
			, CachedAttributeDescriptorIndex(INDEX_NONE)
			, CachedAttributeDependencyIndex(INDEX_NONE)
			, Flags(0)
		{ }
		FGetterItem(FSlateAttributeBase* InAttribute, uint32 InSortOrder, TUniquePtr<SlateAttributePrivate::ISlateAttributeGetter> && InGetter, int16 InAttributeDescriptorIndex)
			: Attribute(InAttribute)
			, Getter(MoveTemp(InGetter))
			, SortOrder(InSortOrder)
			, CachedAttributeDescriptorIndex(InAttributeDescriptorIndex)
			, CachedAttributeDependencyIndex(INDEX_NONE)
			, Flags(0)
		{ }
		FSlateAttributeBase* Attribute;
		TUniquePtr<SlateAttributePrivate::ISlateAttributeGetter> Getter;
		uint32 SortOrder;
		int16 CachedAttributeDescriptorIndex;
		int16 CachedAttributeDependencyIndex;
		union
		{
			struct
			{
				int8 bUpdatedOnce : 1;
				int8 bUpdatedThisFrame : 1;
				int8 bUpatedManually : 1;
				int8 bIsADependencyForSomeoneElse : 1;
				int8 bIsMemberType : 1;
				int8 bIsManagedType : 1;
				int8 bUpdateWhenCollapsed : 1;
			};
			int8 Flags;
		};

		bool operator<(const FGetterItem& Other) const
		{
			return SortOrder < Other.SortOrder;
		}

		EInvalidateWidgetReason GetInvalidationReason(const SWidget&, EInvalidateWidgetReason Reason) const;
	};

	TArray<FGetterItem, TInlineAllocator<4>> Attributes;
	bool bHasUpdatedManuallyFlagToReset = false;
	uint8 CollaspedAttributeCounter = 0;
};
