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
	static void UpdateAllAttributes(SWidget& OwningWidget, EInvalidationPermission InvalidationStyle);
	/**
	 * Update attributes that are responsible to change visibility of the widget.
	 * @param InvalidationStyle if we should invalidate the widget.
	 */
	static void UpdateOnlyVisibilityAttributes(SWidget& OwningWidget, EInvalidationPermission InvalidationStyle);
	/**
	 * Update attributes that are NOT responsible to change visibility of the widget.
	 * @param InvalidationStyle if we should invalidate the widget.
	 */
	static void UpdateExceptVisibilityAttributes(SWidget& OwningWidget, EInvalidationPermission InvalidationStyle);
	/**
	 * Execute UpdateOnlyVisibilityAttributes on every children of the widget.
	 * @param InvalidationStyle if we should invalidate the widget.
	 */
	static void UpdateChildrenOnlyVisibilityAttributes(SWidget& OwningWidget, EInvalidationPermission InvalidationStyle, bool bRecursive);

public:
	bool IsBound(const FSlateAttributeBase& Attribute) const
	{
		return IndexOfAttribute(Attribute) != INDEX_NONE;
	}

	int32 GetRegisteredAttributeCount() const { return Attributes.Num(); }

	int32 GetRegisteredAffectVisibilityAttributeCount() const { return AffectVisibilityCounter; }

	/** Get the name of all the attributes, if available. */
	static TArray<FName> GetAttributeNames(const SWidget& OwningWidget);

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
	void RegisterAttributeImpl(SWidget& OwningWidget, FSlateAttributeBase& Attribute, ESlateAttributeType AttributeType, TUniquePtr<ISlateAttributeGetter>&& Getter);
	bool UnregisterAttributeImpl(const FSlateAttributeBase& Attribute);
	void UpdateAttributesImpl(SWidget& OwningWidget, EInvalidationPermission InvaldiationStyle, int32 StartIndex, int32 IndexNum);
	void SetNeedToResetFlag(int32 Index)
	{
		ResetFlag |= (Index < AffectVisibilityCounter) ? EResetFlags::NeedToReset_OnlyVisibility : EResetFlags::NeedToReset_ExceptVisibility;
	}

private:
	int32 IndexOfAttribute(const FSlateAttributeBase& Attribute) const
	{
		const FSlateAttributeBase* AttributePtr = &Attribute;
		return Attributes.IndexOfByPredicate([AttributePtr](const FGetterItem& Item) { return Item.Attribute == AttributePtr; });
	}

	struct FGetterItem
	{
		using FAttributeIndex = uint8;
		static const FAttributeIndex InvalidAttributeIndex;

		FGetterItem(const FGetterItem&) = delete;
		FGetterItem(FGetterItem&&) = default;
		FGetterItem& operator=(const FGetterItem&) = delete;
		FGetterItem(FSlateAttributeBase* InAttribute, uint32 InSortOrder, TUniquePtr<SlateAttributePrivate::ISlateAttributeGetter>&& InGetter)
			: Attribute(InAttribute)
			, Getter(MoveTemp(InGetter))
			, SortOrder(InSortOrder)
			, CachedAttributeDescriptorIndex(InvalidAttributeIndex)
			, CachedAttributeDependencyIndex(InvalidAttributeIndex)
			, Flags(0)
		{ }
		FGetterItem(FSlateAttributeBase* InAttribute, uint32 InSortOrder, TUniquePtr<SlateAttributePrivate::ISlateAttributeGetter> && InGetter, FAttributeIndex InAttributeDescriptorIndex)
			: Attribute(InAttribute)
			, Getter(MoveTemp(InGetter))
			, SortOrder(InSortOrder)
			, CachedAttributeDescriptorIndex(InAttributeDescriptorIndex)
			, CachedAttributeDependencyIndex(InvalidAttributeIndex)
			, Flags(0)
		{ }
		FSlateAttributeBase* Attribute;
		TUniquePtr<SlateAttributePrivate::ISlateAttributeGetter> Getter;
		uint32 SortOrder;
		FAttributeIndex CachedAttributeDescriptorIndex;
		FAttributeIndex CachedAttributeDependencyIndex;
		union
		{
			struct
			{
				int8 bUpdatedOnce : 1;
				int8 bUpdatedThisFrame : 1;
				int8 bUpatedManually : 1;
				int8 bIsADependencyForSomeoneElse : 1;
				int8 bAffectVisibility : 1;
				ESlateAttributeType AttributeType : 2;
				int8 Unused0 : 1;
				//~if more space is needed bUpatedManually&bUpdatedThisFrame could be merged
			};
			int8 Flags;
		};

		bool operator<(const FGetterItem& Other) const
		{
			return SortOrder < Other.SortOrder;
		}

		EInvalidateWidgetReason GetInvalidationDetail(const SWidget&, EInvalidateWidgetReason Reason) const;

		/** If available, return the name of the attribute. */
		FName GetAttributeName(const SWidget& OwningWidget) const;
	};
	static_assert(sizeof(FGetterItem) <= 32, "The size of FGetterItem is bigger than expected.");

	TArray<FGetterItem, TInlineAllocator<4>> Attributes;
	//~ There is a possibility that the widget has a CachedInvalidationReason and a parent become collapsed.
	//~The invalidation will probably never get executed but
	//~1. The widget is collapsed indirectly, so we do not care if it's invalidated.
	//~2. The parent widget will clear this widget PersistentState.
	EInvalidateWidgetReason CachedInvalidationReason = EInvalidateWidgetReason::None;
	uint8 AffectVisibilityCounter = 0;

	enum class EResetFlags : uint8
	{
		None = 0,
		NeedToReset_OnlyVisibility = (1 << 0),
		NeedToReset_ExceptVisibility = (1 << 1),
	};
	FRIEND_ENUM_CLASS_FLAGS(EResetFlags);
	EResetFlags ResetFlag = EResetFlags::None;
};

ENUM_CLASS_FLAGS(FSlateAttributeMetaData::EResetFlags);
