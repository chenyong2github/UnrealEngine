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

	enum class EInvalidationPermission : uint8
	{
		/** Invalidate the widget if it's needed and it's construction phase is completed. */
		AllowInvalidationIfConstructed,
		/** Invalidate the widget if it's needed. */
		AllowInvalidation,
		/** Cache the invalidation. On any future update, if it's needed, invalidate the widget. */
		DelayInvalidation,
		/** Never invalidate the widget. */
		DenyInvalidation,
		/** Never invalidate the widget and clear any delayed invalidation. */
		DenyAndClearDelayedInvalidation,
	};

	/**
	 * Update all the attributes.
	 * @param InvalidationStyle if we should invalidate the widget.
	 */
	static void UpdateAttributes(SWidget& OwningWidget, EInvalidationPermission InvalidationStyle);
	/**
	 * Update attributes that are mark to be updated when the widget is collapsed.
	 * These attributes are usually responsible to change visibility of the widget.
	 * @param InvalidationStyle if we should invalidate the widget.
	 */
	static void UpdateCollapsedAttributes(SWidget& OwningWidget, EInvalidationPermission InvalidationStyle);
	/**
	 * Update attributes that are mark to be updated when the widget is NOT collapsed.
	 * These attributes usually do not change the visibility of the widget.
	 * @param InvalidationStyle if we should invalidate the widget.
	 */
	static void UpdateExpandedAttributes(SWidget& OwningWidget, EInvalidationPermission InvalidationStyle);
	/**
	 * Update the children attributes that are mark to be updated when the widget is collapsed.
	 * These attributes are usually responsible to change visibility of the widget.
	 * @param InvalidationStyle if we should invalidate the widget.
	 */
	static void UpdateChildrenCollapsedAttributes(SWidget& OwningWidget, EInvalidationPermission InvalidationStyle);

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
	void UpdateAttributesImpl(SWidget& OwningWidget, EUpdateType UpdateType, EInvalidationPermission InvaldiationStyle);

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
	//~ There is a possibility that the widget has a CachedInvalidationReason and a parent become collapsed.
	//~The invalidation will probably never get executed but
	//~1. The widget is collapsed indirectly, so we do not care if it's invalidated.
	//~2. The parent widget will clear this widget PersistentState.
	EInvalidateWidgetReason CachedInvalidationReason = EInvalidateWidgetReason::None;
	bool bHasUpdatedManuallyFlagToReset = false;
	uint8 CollaspedAttributeCounter = 0;
};
