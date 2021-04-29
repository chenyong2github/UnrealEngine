// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Attribute.h"
#include "Templates/EqualTo.h"
#include "Widgets/InvalidateWidgetReason.h"

#include <type_traits>


class SWidget;

/** */
namespace SlateAttributePrivate
{
	/** Predicate used to identify if the InvalidationWidgetReason is defined in the attribute descriptor. */
	struct FSlateAttributeNoInvalidationReason
	{
		static constexpr EInvalidateWidgetReason GetInvalidationReason(const SWidget&) { return EInvalidateWidgetReason::None; }
	};


	/** */
	enum class ESlateAttributeType : uint8
	{
		Member		= 0,	// Member of a SWidget (are not allowed to move).
		Managed		= 1,	// External to the SWidget, global variable or member that can moved.
		Unused0		= 2,
		Unused1		= 3,
		// We use the attribute type in a bit field in FSlateAttributeMetaData. Only 4 value are allowed.
	};


	class ISlateAttributeGetter;
	template<typename ObjectType, typename InvalidationReasonPredicate, typename FComparePredicate, ESlateAttributeType AttributeType>
	struct TSlateAttributeBase;
	struct FSlateAttributeContainer;
	template<typename AttributeMemberType>
	struct TSlateMemberAttributeRef;


	/** */
	class ISlateAttributeGetter
	{
	public:
		struct FUpdateAttributeResult
		{
			FUpdateAttributeResult(EInvalidateWidgetReason InInvalidationReason)
				: InvalidationReason(InInvalidationReason)
				, bInvalidationRequested(true)
			{}
			FUpdateAttributeResult()
				: InvalidationReason(EInvalidateWidgetReason::None)
				, bInvalidationRequested(false)
			{}
			EInvalidateWidgetReason InvalidationReason;
			bool bInvalidationRequested;
		};


		virtual FUpdateAttributeResult UpdateAttribute(const SWidget& Widget) = 0;
		virtual const FSlateAttributeBase& GetAttribute() const = 0;
		virtual void SetAttribute(FSlateAttributeBase&) = 0;
		virtual FDelegateHandle GetDelegateHandle() const = 0;
		virtual ~ISlateAttributeGetter() = default;
	};


	/** */
	struct SLATECORE_API FSlateAttributeImpl : public FSlateAttributeBase
	{
	protected:
		bool ProtectedIsWidgetInDestructionPath(SWidget* Widget) const;
		bool ProtectedIsImplemented(const SWidget& Widget) const;
		void ProtectedUnregisterAttribute(SWidget& Widget, ESlateAttributeType AttributeType) const;
		void ProtectedRegisterAttribute(SWidget& Widget, ESlateAttributeType AttributeType, TUniquePtr<ISlateAttributeGetter>&& Wrapper);
		void ProtectedInvalidateWidget(SWidget& Widget, ESlateAttributeType AttributeType, EInvalidateWidgetReason InvalidationReason) const;
		bool ProtectedIsBound(const SWidget& Widget, ESlateAttributeType AttributeType) const;
		ISlateAttributeGetter* ProtectedFindGetter(const SWidget& Widget, ESlateAttributeType AttributeType) const;
		FDelegateHandle ProtectedFindGetterHandle(const SWidget& Widget, ESlateAttributeType AttributeType) const;
		bool ProtectedIsIdenticalTo(const SWidget& Widget, ESlateAttributeType AttributeType, const FSlateAttributeBase& Other, const bool bHasSameValue) const;
		bool ProtectedIsIdenticalToAttribute(const SWidget& Widget, ESlateAttributeType AttributeType, const void* Other, const bool bHasSameValue) const;
		void ProtectedUpdateNow(SWidget& Widget, ESlateAttributeType AttributeType);
		void ProtectedMoveAttribute(SWidget& Widget, ESlateAttributeType AttributeType, const FSlateAttributeBase* Other);
	};

} // SlateAttributePrivate
