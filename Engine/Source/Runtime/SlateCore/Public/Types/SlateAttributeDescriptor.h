// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Attribute.h"
#include "Widgets/InvalidateWidgetReason.h"

#include <type_traits>


class FSlateWidgetClassData;
namespace SlateAttributePrivate
{
	enum class ESlateAttributeType : uint8;
}


/**
 * Describes the static information about a Widget's type SlateAttributes.
 **/
class SLATECORE_API FSlateAttributeDescriptor
{
public:
	/**
	 * A EInvalidationWidgetReason Attribute 
	 * It can be explicitly initialize or can be a callback static function or lambda that returns the EInvalidationReason.
	 * The signature of the function takes a const SWidget& as argument.
	 */
	struct FInvalidateWidgetReasonAttribute
	{
		friend FSlateAttributeDescriptor;

		using Arg1Type = const class SWidget&;
		DECLARE_DELEGATE_RetVal_OneParam(EInvalidateWidgetReason, FGetter, Arg1Type);

		FInvalidateWidgetReasonAttribute(const FInvalidateWidgetReasonAttribute&) = default;
		FInvalidateWidgetReasonAttribute(FInvalidateWidgetReasonAttribute&&) = default;
		FInvalidateWidgetReasonAttribute& operator=(const FInvalidateWidgetReasonAttribute&) = default;
		FInvalidateWidgetReasonAttribute& operator=(FInvalidateWidgetReasonAttribute&&) = default;

		/** Default constructor. */
		explicit FInvalidateWidgetReasonAttribute(EInvalidateWidgetReason InReason)
			: Reason(InReason)
			, Getter()
		{
		}

		template<typename... PayloadTypes>
		explicit FInvalidateWidgetReasonAttribute(typename FGetter::template FStaticDelegate<PayloadTypes...>::FFuncPtr InFuncPtr, PayloadTypes&&... InputPayload)
			: Reason(EInvalidateWidgetReason::None)
			, Getter(FGetter::CreateStatic(InFuncPtr, Forward<PayloadTypes>(InputPayload)...))
		{
		}

		template<typename LambdaType, typename... PayloadTypes>
		explicit FInvalidateWidgetReasonAttribute(LambdaType&& InCallable, PayloadTypes&&... InputPayload)
			: Reason(EInvalidateWidgetReason::None)
			, Getter(FGetter::CreateLambda(InCallable, Forward<PayloadTypes>(InputPayload)...))
		{
		}

		bool IsBound() const
		{
			return Getter.IsBound();
		}

		EInvalidateWidgetReason Get(const SWidget& Widget) const
		{
			return IsBound() ? Getter.Execute(Widget) : Reason;
		}

	private:
		EInvalidateWidgetReason Reason;
		FGetter Getter;
	};

	/** */
	DECLARE_DELEGATE_OneParam(FAttributeValueChangedDelegate, SWidget& /*Widget*/);

	/** */
	enum class ECallbackOverrideType
	{
		/** Replace the callback that the base class defined. */
		ReplacePrevious,
		/** Execute the callback that the base class defined, then execute the new callback. */
		ExecuteAfterPrevious,
		/** Execute the new callback, then execute the callback that the base class defined. */
		ExecuteBeforePrevious,
	};

public:
	/** */
	struct FAttribute;
	struct FInitializer;

	/** */
	using OffsetType = uint32;

	/** The default sort order that define in which order attributes will be updated. */
	static uint32 DefaultSortOrder(OffsetType Offset) { return Offset * 100; }

	/** */
	struct FAttribute
	{
		friend FSlateAttributeDescriptor;
		friend FInitializer;

	public:
		FAttribute(FName Name, OffsetType Offset, FInvalidateWidgetReasonAttribute Reason);

		FName GetName() const { return Name; }
		uint32 GetSortOrder() const { return SortOrder; }
		EInvalidateWidgetReason GetInvalidationReason(const SWidget& Widget) const { return InvalidationReason.Get(Widget); }
		SlateAttributePrivate::ESlateAttributeType GetAttributeType() const { return AttributeType; }
		bool DoesAffectVisibility() const { return bAffectVisibility; }

		void ExecuteOnValueChangedIfBound(SWidget& Widget) const { OnValueChanged.ExecuteIfBound(Widget); }

	private:
		FName Name;
		OffsetType Offset;
		FName Prerequisite;
		uint32 SortOrder;
		FInvalidateWidgetReasonAttribute InvalidationReason;
		FAttributeValueChangedDelegate OnValueChanged;
		SlateAttributePrivate::ESlateAttributeType AttributeType;
		bool bAffectVisibility;
	};

	/** Internal class to initialize the SlateAttributeDescriptor (Add attributes or modify existing attributes). */
	struct SLATECORE_API FInitializer
	{
	private:
		friend FSlateWidgetClassData;
		FInitializer(FSlateAttributeDescriptor& InDescriptor);
		FInitializer(FSlateAttributeDescriptor& InDescriptor, const FSlateAttributeDescriptor& ParentDescriptor);
		FInitializer(const FInitializer&) = delete;
		FInitializer& operator=(const FInitializer&) = delete;

	public:
		~FInitializer();

		struct SLATECORE_API FAttributeEntry
		{
			FAttributeEntry(FSlateAttributeDescriptor& Descriptor, int32 InAttributeIndex);

			/**
			 * Update the attribute after the prerequisite.
			 * The order is guaranteed but other attributes may be updated in between.
			 * No order is guaranteed if the prerequisite or this property is updated manually.
			 */
			FAttributeEntry& UpdatePrerequisite(FName Prerequisite);

			/**
			 * The attribute affect the visibility of the widget.
			 * We only update the attributes that can change the visibility of the widget when the widget is collapsed.
			 * Attributes that affect visibility must have the Visibility attribute as a Prerequisite or the Visibility attribute must have it as a Prerequisite.
			 */
			FAttributeEntry& AffectVisibility();

			/**
			 * Notified when the attribute value changed.
			 * It's preferable that you delay any action to the Tick or Paint function.
			 * You are not allowed to make changes that would affect the SWidget ChildOrder or its Visibility.
			 * It will not be called when the SWidget is in his construction phase.
			 * @see SWidget::IsConstructed
			 */
			FAttributeEntry& OnValueChanged(FAttributeValueChangedDelegate Callback);

		private:
			FSlateAttributeDescriptor& Descriptor;
			int32 AttributeIndex;
		};

		FAttributeEntry AddMemberAttribute(FName AttributeName, OffsetType Offset, const FInvalidateWidgetReasonAttribute& ReasonGetter);
		FAttributeEntry AddMemberAttribute(FName AttributeName, OffsetType Offset, FInvalidateWidgetReasonAttribute&& ReasonGetter);

		/** Change the InvalidationReason of an attribute defined in a base class. */
		void OverrideInvalidationReason(FName AttributeName, const FInvalidateWidgetReasonAttribute& Reason);
		/** Change the InvalidationReason of an attribute defined in a base class. */
		void OverrideInvalidationReason(FName AttributeName, FInvalidateWidgetReasonAttribute&& Reason);

		/** Change the FAttributeValueChangedDelegate of an attribute defined in a base class. */
		void OverrideOnValueChanged(FName AttributeName, ECallbackOverrideType OverrideType, FAttributeValueChangedDelegate Callback);

		/** Change the update type of an attribute defined in a base class. */
		void SetAffectVisibility(FName AttributeName, bool bAffectVisibility);


	private:
		FSlateAttributeDescriptor& Descriptor;
	};

	/** @returns the number of Attributes registered. */
	int32 GetAttributeNum() const { return Attributes.Num(); }

	/** @returns the Attribute at the index previously found with IndexOfMemberAttribute */
	const FAttribute& GetAttributeAtIndex(int32 Index) const;

	/** @returns the Attribute with the corresponding name. */
	const FAttribute* FindAttribute(FName AttributeName) const;

	/** @returns the Attribute of a SlateAttribute that have the corresponding memory offset. */
	const FAttribute* FindMemberAttribute(OffsetType AttributeOffset) const;

	/** @returns the index of a SlateAttribute that have the corresponding memory offset. */
	int32 IndexOfAttribute(FName AttributeName) const;

	/** @returns the index of a SlateAttribute that have the corresponding memory offset. */
	int32 IndexOfMemberAttribute(OffsetType AttributeOffset) const;

private:
	FAttribute* FindAttribute(FName AttributeName);

	FInitializer::FAttributeEntry AddMemberAttribute(FName AttributeName, OffsetType Offset, FInvalidateWidgetReasonAttribute ReasonGetter);
	void OverrideInvalidationReason(FName AttributeName, FInvalidateWidgetReasonAttribute ReasonGetter);
	void OverrideOnValueChanged(FName AttributeName, ECallbackOverrideType OverrideType, FAttributeValueChangedDelegate Callback);
	void SetPrerequisite(FAttribute& Attribute, FName Prerequisite);
	void SetAffectVisibility(FAttribute& Attribute, bool bUpdate);

private:
	TArray<FAttribute> Attributes;
};

/**
 * Add a TSlateAttribute to the descriptor.
 * @param _Initializer The FSlateAttributeInitializer from the PrivateRegisterAttributes function.
 * @param _Property The TSlateAttribute property
 * @param _Reason The EInvalidationWidgetReason or a static function/lambda that takes a const SWidget& and that returns the invalidation reason.
 */
#define SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION_WITH_NAME(_Initializer, _Name, _Property, _Reason) \
		static_assert(decltype(_Property)::IsMemberType, "The SlateProperty is not a TSlateAttribute. Do not use SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION"); \
		static_assert(!decltype(_Property)::HasDefinedInvalidationReason, "When implementing the SLATE_DECLARE_WIDGET pattern, use TSlateAttribute without the invalidation reason."); \
		static_assert(!std::is_same<decltype(_Reason), EInvalidateWidgetReason>::value || FSlateAttributeBase::IsInvalidateWidgetReasonSupported(_Reason), "The invalidation is not supported by the SlateAttribute."); \
		_Initializer.AddMemberAttribute(_Name, STRUCT_OFFSET(PrivateThisType, _Property), FSlateAttributeDescriptor::FInvalidateWidgetReasonAttribute{_Reason})

#define SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION(_Initializer, _Property, _Reason) \
	SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION_WITH_NAME(_Initializer, GET_MEMBER_NAME_CHECKED(PrivateThisType, _Property), _Property, _Reason)

using FSlateAttributeInitializer = FSlateAttributeDescriptor::FInitializer;
