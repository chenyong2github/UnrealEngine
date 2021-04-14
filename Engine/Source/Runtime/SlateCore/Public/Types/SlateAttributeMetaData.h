// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Algo/BinarySearch.h"
#include "Types/ISlateMetaData.h"
#include "Types/SlateAttribute.h"
#include "Types/SlateAttributeDescriptor.h"
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
	/** @return the instance associated to the SWidget (if it exists). */
	static FSlateAttributeMetaData* FindMetaData(const SWidget& OwningWidget);
	/**
	 * Update all the attributes.
	 * Invalidate the widget if it has finished its construction phase.
	 */
	static void UpdateAttributes(SWidget& OwningWidget);
	/**
	 * Update attributes that are mark to be updated when the widget is collapsed.
	 * Invalidate the widget if it has finished its construction phase.
	 */
	static void UpdateCollapsedAttributes(SWidget& OwningWidget);
	/**
	 * Update attributes that are mark to be updated when the widget is NOT collapsed.
	 * Invalidate the widget if it has finished its construction phase.
	 */
	static void UpdateExpandedAttributes(SWidget& OwningWidget);
	/**
	 * Update attributes that are mark to be updated when the widget is collapsed.
	 * @param bAllowInvalidation if we should allow the widget to be invalidated.
	 */
	static void UpdateAttributes(SWidget& OwningWidget, bool bAllowInvalidation);
	/**
	 * Update attributes that are mark to be updated when the widget is collapsed.
	 * @param bAllowInvalidation if we should allow the widget to be invalidated.
	 */
	static void UpdateCollapsedAttributes(SWidget& OwningWidget, bool bAllowInvalidation);
	/**
	 * Update attributes that are mark to be updated when the widget is NOT collapsed.
	 * @param bAllowInvalidation if we should allow the widget to be invalidated.
	 */
	static void UpdateExpandedAttributes(SWidget& OwningWidget, bool bAllowInvalidation);

public:
	bool IsBound(const FSlateAttributeBase& Attribute) const
	{
		return IndexOfAttribute(Attribute) != INDEX_NONE;
	}

	int32 RegisteredAttributeCount() const { return Attributes.Num(); }

	int32 RegisteredCollaspedAttributeCount() const { return CollaspedAttributeCounter; }

	/** Get the name of all the attributes, if available. */
	TArray<FName> GetAttributeNames(const SWidget& OwningWidget) const;

private:
	using ESlateAttributeType = SlateAttributePrivate::ESlateAttributeType;
	using ISlateAttributeGetter = SlateAttributePrivate::ISlateAttributeGetter;
	static void RegisterAttribute(SWidget& OwningWidget, FSlateAttributeBase& Attribute, ESlateAttributeType AttributeType, TUniquePtr<ISlateAttributeGetter>&& Wrapper);
	static bool UnregisterAttribute(SWidget& OwningWidget, const FSlateAttributeBase& Attribute);
	static void InvalidateWidget(SWidget& OwningWidget, const FSlateAttributeBase& Attribute, ESlateAttributeType AttributeType, EInvalidateWidgetReason Reason);
	static void UpdateAttribute(SWidget& OwningWidget, FSlateAttributeBase& Attribute);
	static bool IsAttributeBound(const SWidget& OwningWidget, const FSlateAttributeBase& Attribute);
	static SlateAttributePrivate::ISlateAttributeGetter* GetAttributeGetter(const SWidget& OwningWidget, const FSlateAttributeBase& Attribute);
	static FDelegateHandle GetAttributeGetterHandle(const SWidget& OwningWidget, const FSlateAttributeBase& Attribute);
	static void MoveAttribute(const SWidget& OwningWidget, FSlateAttributeBase& NewAttribute, ESlateAttributeType AttributeType, const FSlateAttributeBase* PreviousAttribute);

private:
	enum class EUpdateType
	{
		All,
		Collapsed,
		Expanded, // not mark as collapsed
	};
	void RegisterAttributeImpl(SWidget& OwningWidget, FSlateAttributeBase& Attribute, ESlateAttributeType AttributeType, TUniquePtr<ISlateAttributeGetter>&& Getter);
	bool UnregisterAttributeImpl(const FSlateAttributeBase& Attribute);
	void UpdateAttributesImpl(SWidget& OwningWidget, EUpdateType UpdateType, bool bAllowInvalidation);

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

		using FInvalidationDetail = TTuple<const FSlateAttributeDescriptor::FInvalidationDelegate*, EInvalidateWidgetReason>;
		FInvalidationDetail GetInvalidationDetail(const SWidget&, EInvalidateWidgetReason Reason) const;

		/** If available, return the name of the attribute. */
		FName GetAttributeName(const SWidget& OwningWidget) const;
	};

	TArray<FGetterItem, TInlineAllocator<4>> Attributes;
	bool bHasUpdatedManuallyFlagToReset = false;
	uint8 CollaspedAttributeCounter = 0;
};
