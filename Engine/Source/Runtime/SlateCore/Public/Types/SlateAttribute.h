// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Attribute.h"
#include "Templates/EqualTo.h"
#include "Widgets/InvalidateWidgetReason.h"

#include <type_traits>

#ifndef UE_SLATE_WITH_MEMBER_ATTRIBUTE_DEBUGGING
	#define UE_SLATE_WITH_MEMBER_ATTRIBUTE_DEBUGGING UE_BUILD_DEBUG
#endif
#ifndef UE_SLATE_WITH_ATTRIBUTE_INITIALIZATION_ON_BIND
	#define UE_SLATE_WITH_ATTRIBUTE_INITIALIZATION_ON_BIND 0
#endif

/**
 * Use TSlateAttribute when it's a SWidget member.
 * Use TSlateManagedAttribute when it's a member inside an array or other moving structure, and the array is a SWidget member. They can only be moved (they can't be copied). THey consume more memory.
 * For everything else, use TAttribute.
 *
 *
 *
 * In Slate, TAttributes are optimized for developer efficiency.
 * They enable widgets to poll for data instead of requiring the user to manually set the state on widgets.
 * Attributes generally work well when performance is not a concern but break down when performance is critical (like a game UI).
 *
 * The invalidation system allows only widgets that have changed to perform expensive Slate layout.
 * Bound TAttributes are incompatible with invalidation because we do not know when the data changes.
 * Additionally TAttributes for common functionality such as visibility are called multiple times per frame and the delegate overhead for that alone is very high.
 * TAttributes have a high memory overhead and are not cache-friendly.
 *
 * TSlateAttribute makes the attribute system viable for invalidation and more performance-friendly while keeping the benefits of attributes intact.
 * TSlateAttributes are updated once per frame in the Prepass update phase. If the cached value of the TSlateAttribute changes, then it will invalidate the widget.
 * The TSlateAttributes are updated in the order the variables are defined in the SWidget definition (by default).
 * TSlateManagedAttribute are updated in a random order (after TSlateAttribute).
 * The update order of TSlateAttribute can be defined/override by setting a Prerequisite (see bellow for an example).
 * The invalidation reason can be a predicate (see bellow for an example).
 * The invalidation reason can be override per SWidget. Use override with precaution since it can break the invalidation of widget's parent.
 * The widget attributes are updated only if the widget is visible/not collapsed.
 *
 *
 * TSlateAttribute is not copyable and can only live inside a SWidget.
 * For performance reasons, we do not save the extra information that would be needed to be "memory save" in all contexts.
 * If you create a TSlateAttribute that can be moved, you need to use TSlateManagedAttribute.
 * TSlateManagedAttribute is as fast as TSlateAttribute but use more memory and is less cache-friendly.
 * Note, if you use TAttribute to change the state of a SWidget, you need to override bool ComputeVolatility() const.
 * Note, ComputeVolatility() is not needed for TSlateAttribute and TSlateManagedAttribute
 *
 * TSlateAttribute request a SWidget pointer. The "this" pointer should ALWAYS be used.
 * (The SlateAttribute pointer is saved inside the SlateAttributeMetaData. The widget needs to be aware when the pointer changes.)
 *
 *
 *	.h
 *  // The new way of using attribute in your SWidget
 *	class SMyNewWidget : public SLeafWidget
 *	{
 *		SLATE_DECLARE_WIDGET(SMyNewWidget, SLeafWidget)	// It defined the static data needed by the SlateAttribute system.
 *														// The invalidation reason is defined in the static function "void PrivateRegisterAttributes(FSlateAttributeInitializer& AttributeInitializer)"
 *														// You need to implement the function PrivateRegisterAttributes.
 *														// If you can't implement the SLATE_DECLARE_WIDGET pattern, use TSlateAttribute and provide a reason instead.
 *
 *		SLATE_BEGIN_ARGS(SMyNewWidget)
 *			SLATE_ATTRIBUTE(FLinearColor, Color)
 *		SLATE_END_ARGS()
 *
 *		SMyNewWidget()
 *			: NewWay(*this, FLinearColor::White) // Always use "this" pointer. No exception. If you can't, use TAttribute instead.
 *		{}
 *
 *		virtual int32 OnPaint( const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled ) const override
 *		{
 *			++LayerId;
 *			FSlateDrawElement::MakeDebugQuad(
 *				OutDrawElements,
 *				LayerId,
 *				AllottedGeometry.ToPaintGeometry(),
 *				NewWay.Get()							// The value is already cached.
 *			);
 *			return LayerId;
 *		}
 *
 *		// bEnabled do not invalidate Layout. We need to override the default invalidation.
 *		virtual FVector2D ComputeDesiredSize(float) const override { return IsEnabled() ? FVector2D(10.f, 10.f) : FVector2D(20.f, 20.f); }
 *
 *		void Construct(const FArguments& InArgs)
 *		{
 *			// Assigned the attribute if needed.
 *			// If the TAttribute is bound, copy the TSlateAttribute to the TAttribute getter, update the value, test if the value have changed and, if so, invalidate the widget.
 *			// It the TAttribute is set, remove previous getter, set the TSlateAttribute value, test if the value changed and, if so, invalidate the widget.
 *			// Else, nothing (use the previous value).
 *			NewWays.Bind(*this, InArgs._Color); // Always use the "this" pointer.
 *		}
 *
 *		// If NewWay is bounded and the values changed from the previous frame, the widget will be invalidated.
 *		//In this case, it will only paint the widget (no layout required).
 *		TSlateAttribute<FLinearColor> NewWay;
 *
 *		// Note that you can put a predicate or put the invalidation as a template argument.
 *		//Either you define the attribute in PrivateRegisterAttributes or you define the invalidation at declaration
 *		//but you need to use one and one of the 2 methods. Compile and runtime check will check if you did it properly.
 *		//Setting the reason as a template argument is less error prone (ie. missing from the PrivateRegisterAttributes, bad copy paste, bad reason...)
 *		//Setting the reason in PrivateRegisterAttributes enable override and ease debugging later (ie. the attribute will be named)
 *		//TSlateAttribute<FLinearColor, EInvalidationReason::Paint> NewWayWithout_SLATE_DECLARE_WIDGET_;
 *	};
 *
 *  .cpp
 *	// This is optional. It is still a good practice to implement it since it will allow user to override the default behavior and control the update order.
 *	//The WidgetReflector use that information.
 *	//See NewWayWithout_SLATE_DECLARE_WIDGET_ for an example of how to use the system without the SLATE_DECLARE_WIDGET
 *
 *	SLATE_IMPLEMENT_WIDGET(STextBlock)
 *	void STextBlock::PrivateRegisterAttributes(FSlateAttributeInitializer& AttributeInitializer)
 *	{
 *		SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION(AttributeInitializer, NewWay, EInvalidationReason::Paint)
 *			.SetPrerequisite("bEnabled"); // SetPrerequisite is not needed here. This is just an example to show how you could do it if needed.
 *
 *		//bEnabled invalidate paint, we need it to invalidate the Layout.
 *		AttributeInitializer.OverrideInvalidationReason("bEnabled",
			FSlateAttributeDescriptor::FInvalidateWidgetReasonAttribute{EWidgetInvalidationReason::Layout | EWidgetInvalidationReason::Paint});
 *	}
 *
 *
 *	// We used to do this. Keep using this method when you are not able to provide the "this" pointer to TSlateAttribute.
 *	class SMyOldWidget : public SLeafWidget
 *	{
 *		SLATE_BEGIN_ARGS(SMyOldWidget)
 *			SLATE_ATTRIBUTE(FLinearColor, Color)
 *		SLATE_END_ARGS()
 *
 *		// Widget will be updated every frame if a TAttribute is bound. (Even if the value didn't change).
 *		virtual bool ComputeVolatility() const override { return SLeafWidget::ComputeVolatility() || OldWay.IsBound(); }
 *
 *		virtual int32 OnPaint( const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled ) const override
 *		{
 *			++LayerId;
 *			FSlateDrawElement::MakeDebugQuad(
 *				OutDrawElements,
 *				LayerId,
 *				AllottedGeometry.ToPaintGeometry(),
 *				OldWay.Get(FLinearColor::White)		// Fetch the value of OldWays and use White, if the value is not set.
 *			);
 *			return LayerId;
 *		}
 *
 *		virtual FVector2D ComputeDesiredSize(float) const override { return IsEnabled() ? FVector2D(10.f, 10.f) : FVector2D(20.f, 20.f); }
 *
 *		void Construct(const FArguments& InArgs)
 *		{
 *			NewWays = InArgs._Color;
 *		}
 *
 *	private:
 *		TAttribute<FLinearColor> OldWay;
 * };
 */


class SWidget;


/** Default predicate to compare FText. */
struct TSlateAttributeFTextComparePredicate
{
	bool operator()(const FText& Lhs, const FText& Rhs) const
	{
		return Lhs.IdenticalTo(Rhs, ETextIdenticalModeFlags::DeepCompare | ETextIdenticalModeFlags::LexicalCompareInvariants);
	}
};


/** Predicate that returns the InvalidationReason defined as argument type. */
template<EInvalidateWidgetReason InvalidationReason>
struct TSlateAttributeInvalidationReason
{
	static constexpr EInvalidateWidgetReason GetInvalidationReason(const SWidget&) { return InvalidationReason; }
};


/** A structure used to help the user identify deprecated TAttribute that are now TSlateAttribute. */
template<typename ObjectType>
struct FSlateDeprecatedTAttribute
{
	FSlateDeprecatedTAttribute() = default;

	using FGetter = typename TAttribute<ObjectType>::FGetter;

	template<typename OtherType>
	FSlateDeprecatedTAttribute(const OtherType& InInitialValue)	{ }

	FSlateDeprecatedTAttribute(ObjectType&& InInitialValue)	{ }

	template<class SourceType>
	FSlateDeprecatedTAttribute(TSharedRef<SourceType> InUserObject, typename FGetter::template TSPMethodDelegate_Const< SourceType >::FMethodPtr InMethodPtr) {}

	template< class SourceType >
	FSlateDeprecatedTAttribute(SourceType* InUserObject, typename FGetter::template TSPMethodDelegate_Const< SourceType >::FMethodPtr InMethodPtr) { }

	bool IsSet() const { return false; }

	template<typename OtherType>
	void Set(const OtherType& InNewValue) {}

	const ObjectType& Get(const ObjectType& DefaultValue) const { return DefaultValue; }
	const ObjectType& Get() const { static ObjectType Temp; return Temp; }
	FGetter GetBinding() const { return false; }

	void Bind(const FGetter& InGetter) {}
	bool IsBound() const { return false; }

	bool IdenticalTo(const TAttribute<ObjectType>& InOther) const { return false; }
};


/** Base struct of all SlateAttribute type. */
struct FSlateAttributeBase
{

};


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
		Member = 0,
		Managed = 1,
	};

	class ISlateAttributeGetter;
	template<typename ObjectType, typename InvalidationReasonPredicate, typename FComparePredicate, ESlateAttributeType AttributeType>
	struct TSlateAttributeBase;
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


	/**
	 * Attribute object
	 * InObjectType - Type of the value to store
	 * InInvalidationReasonPredicate - Predicate that returns the type of invalidation to do when the value changes (e.g layout or paint)
	 *								The invalidation can be overridden per widget. (This use memory allocation. See FSlateAttributeMetadata.)
	 * InComparePredicateType - Predicate to compare the cached value with the Getter.
	 * bInIsExternal - The attribute life is not controlled by the SWidget.
	 */
	template<typename InObjectType, typename InInvalidationReasonPredicate, typename InComparePredicateType, ESlateAttributeType InAttributeType>
	struct TSlateAttributeBase : public FSlateAttributeImpl
	{
	public:
		template<typename AttributeMemberType>
		friend struct TSlateMemberAttributeRef;

		using ObjectType = InObjectType;
		using FInvalidationReasonPredicate = InInvalidationReasonPredicate;
		using FGetter = typename TAttribute<ObjectType>::FGetter;
		using FComparePredicate = InComparePredicateType;

		static EInvalidateWidgetReason GetInvalidationReason(const SWidget& Widget) { return FInvalidationReasonPredicate::GetInvalidationReason(Widget); }
		
	private:
		void UpdateNowOnBind(SWidget& Widget)
		{
#if UE_SLATE_WITH_ATTRIBUTE_INITIALIZATION_ON_BIND
			ProtectedUpdateNow(Widget, InAttributeType);
#endif
		}

		void VerifyOwningWidget(const SWidget& Widget) const
		{
#if UE_SLATE_WITH_MEMBER_ATTRIBUTE_DEBUGGING
			checkf(&Widget == Debug_OwningWidget,
				TEXT("The Owning Widget is not the same as used at construction. This will cause bad memory access."));
#endif
		}

	public:
		TSlateAttributeBase() = default;

		TSlateAttributeBase(SWidget& Widget)
			: Value()
#if UE_SLATE_WITH_MEMBER_ATTRIBUTE_DEBUGGING
			, Debug_OwningWidget(&Widget)
#endif
		{
		}

		TSlateAttributeBase(SWidget& Widget, const ObjectType& InValue)
			: Value(InValue)
#if UE_SLATE_WITH_MEMBER_ATTRIBUTE_DEBUGGING
			, Debug_OwningWidget(&Widget)
#endif
		{
		}

		TSlateAttributeBase(SWidget& Widget, ObjectType&& InValue)
			: Value(MoveTemp(InValue))
#if UE_SLATE_WITH_MEMBER_ATTRIBUTE_DEBUGGING
			, Debug_OwningWidget(&Widget)
#endif
		{
		}

		TSlateAttributeBase(SWidget& Widget, const FGetter& Getter, const ObjectType& InitialValue)
			: Value(InitialValue)
#if UE_SLATE_WITH_MEMBER_ATTRIBUTE_DEBUGGING
			, Debug_OwningWidget(&Widget)
#endif
		{
			if (Getter.IsBound())
			{
				ConstructWrapper(Widget, Getter);
			}
		}

		TSlateAttributeBase(SWidget& Widget, const FGetter& Getter, ObjectType&& InitialValue)
			: Value(MoveTemp(InitialValue))
#if UE_SLATE_WITH_MEMBER_ATTRIBUTE_DEBUGGING
			, Debug_OwningWidget(&Widget)
#endif
		{
			if (Getter.IsBound())
			{
				ConstructWrapper(Widget, Getter);
			}
		}

		TSlateAttributeBase(SWidget& Widget, FGetter&& Getter, const ObjectType& InitialValue)
			: Value(InitialValue)
#if UE_SLATE_WITH_MEMBER_ATTRIBUTE_DEBUGGING
			, Debug_OwningWidget(&Widget)
#endif
		{
			if (Getter.IsBound())
			{
				ConstructWrapper(Widget, MoveTemp(Getter));
			}
		}

		TSlateAttributeBase(SWidget& Widget, FGetter&& Getter, ObjectType&& InitialValue)
			: Value(MoveTemp(InitialValue))
#if UE_SLATE_WITH_MEMBER_ATTRIBUTE_DEBUGGING
			, Debug_OwningWidget(&Widget)
#endif
		{
			if (Getter.IsBound())
			{
				ConstructWrapper(Widget, MoveTemp(Getter));
			}
		}

		TSlateAttributeBase(SWidget& Widget, const TAttribute<ObjectType>& Attribute, const ObjectType& InitialValue)
			: Value((Attribute.IsSet() && !Attribute.IsBound()) ? Attribute.Get() : InitialValue)
#if UE_SLATE_WITH_MEMBER_ATTRIBUTE_DEBUGGING
			, Debug_OwningWidget(&Widget)
#endif
		{
			if (Attribute.IsBound())
			{
				ConstructWrapper(Widget, Attribute.GetBinding());
			}
		}

		TSlateAttributeBase(SWidget& Widget, TAttribute<ObjectType>&& Attribute, ObjectType&& InitialValue)
			: Value((Attribute.IsSet() && !Attribute.IsBound()) ? MoveTemp(Attribute.Steal().template Get<ObjectType>()) : MoveTemp(InitialValue))
#if UE_SLATE_WITH_MEMBER_ATTRIBUTE_DEBUGGING
			, Debug_OwningWidget(&Widget)
#endif
		{
			if (Attribute.IsBound())
			{
				ConstructWrapper(Widget, MoveTemp(Attribute.Steal().template Get<FGetter>()));
			}
		}

	public:
		/** @return the SlateAttribute cached value. If the SlateAttribute is bound, the value will be cached at the end of the every frame. */ 
		UE_NODISCARD const ObjectType& Get() const
		{
			return Value;
		}
		
		/** Update the cached value and invalidate the widget if needed. */
		void UpdateNow(SWidget& Widget)
		{
			VerifyOwningWidget(Widget);
			ProtectedUpdateNow(Widget, InAttributeType);
		}

	public:
		/** Unbind the SlateAttribute and set its value. It may invalidate the Widget if the value is different. */
		void Set(SWidget& Widget, const ObjectType& NewValue)
		{
			VerifyOwningWidget(Widget);
			ProtectedUnregisterAttribute(Widget, InAttributeType);

			const bool bIsIdentical = FComparePredicate{}.operator()(Value, NewValue);
			if (!bIsIdentical)
			{
				Value = NewValue;
				ProtectedInvalidateWidget(Widget, InAttributeType, GetInvalidationReason(Widget));
			}
		}

		/** Unbind the SlateAttribute and set its value. It may invalidate the Widget if the value is different. */
		void Set(SWidget& Widget, ObjectType&& NewValue)
		{
			VerifyOwningWidget(Widget);
			ProtectedUnregisterAttribute(Widget, InAttributeType);

			const bool bIsIdentical = FComparePredicate{}.operator()(Value, NewValue);
			if (!bIsIdentical)
			{
				Value = MoveTemp(NewValue);
				ProtectedInvalidateWidget(Widget, InAttributeType, GetInvalidationReason(Widget));
			}
		}

	public:
		/**
		 * Bind the SlateAttribute to the Getter function.
		 * (If enabled) Update the value from the Getter. If the value is different, then invalidate the widget.
		 * The SlateAttribute will now be updated every frame from the Getter.
		 */
		void Bind(SWidget& Widget, const FGetter& Getter)
		{
			VerifyOwningWidget(Widget);
			if (Getter.IsBound())
			{
				AssignBinding(Widget, Getter);
			}
			else
			{
				ProtectedUnregisterAttribute(Widget, InAttributeType);
			}
		}

		/**
		 * Bind the SlateAttribute to the Getter function.
		 * (If enabled) Update the value from the Getter. If the value is different, then invalidate the widget.
		 * The SlateAttribute will now be updated every frame from the Getter.
		 */
		void Bind(SWidget& Widget, FGetter&& Getter)
		{
			VerifyOwningWidget(Widget);
			if (Getter.IsBound())
			{
				AssignBinding(Widget, MoveTemp(Getter));
			}
			else
			{
				ProtectedUnregisterAttribute(Widget, InAttributeType);
			}
		}

		/**
		 * Bind the SlateAttribute to the newly create getter function.
		 * (If enabled) Update the value from the getter. If the value is different, then invalidate the widget.
		 * The SlateAttribute will now be updated every frame from the Getter.
		 */
		template<typename WidgetType, typename U = typename std::enable_if<std::is_base_of<SWidget, WidgetType>::value>::type>
		void Bind(WidgetType& Widget, typename FGetter::template TSPMethodDelegate_Const<WidgetType>::FMethodPtr MethodPtr)
		{
			Bind(Widget, FGetter::CreateSP(&Widget, MethodPtr));
		}

		/**
		 * Bind the SlateAttribute to the Attribute Getter function (if it exist).
		 * (If enabled) Update the value from the getter. If the value is different, then invalidate the widget.
		 * The SlateAttribute will now be updated every frame from the Getter.
		 * Or
		 * Set the SlateAttribute's value if the Attribute is not bound but is set.
		 * This will Unbind any previously bound getter.
		 * If the value is different, then invalidate the widget.
		 * Or
		 * Unbind the SlateAttribute if the Attribute is not bound and not set.
		 * @see Set
		 */
		void Assign(SWidget& Widget, const TAttribute<ObjectType>& OtherAttribute)
		{
			VerifyOwningWidget(Widget);
			if (OtherAttribute.IsBound())
			{
				AssignBinding(Widget, OtherAttribute.GetBinding());
			}
			else if (OtherAttribute.IsSet())
			{
				Set(Widget, OtherAttribute.Get());
			}
			else
			{
				ProtectedUnregisterAttribute(Widget, InAttributeType);
			}
		}

		void Assign(SWidget& Widget, TAttribute<ObjectType>&& OtherAttribute)
		{
			VerifyOwningWidget(Widget);
			if (OtherAttribute.IsBound())
			{
				AssignBinding(Widget, MoveTemp(OtherAttribute.Steal().template Get<FGetter>()));
			}
			else if (OtherAttribute.IsSet())
			{
				Set(Widget, MoveTemp(OtherAttribute.Steal().template Get<ObjectType>()));
			}
			else
			{
				ProtectedUnregisterAttribute(Widget, InAttributeType);
			}
		}

		void Assign(SWidget& Widget, const TAttribute<ObjectType>& OtherAttribute, const ObjectType& DefaultValue)
		{
			VerifyOwningWidget(Widget);
			if (OtherAttribute.IsBound())
			{
				AssignBinding(Widget, OtherAttribute.GetBinding());
			}
			else if (OtherAttribute.IsSet())
			{
				Set(Widget, OtherAttribute.Get());
			}
			else
			{
				Set(Widget, DefaultValue);
			}
		}

		void Assign(SWidget& Widget, TAttribute<ObjectType>&& OtherAttribute, const ObjectType& DefaultValue)
		{
			VerifyOwningWidget(Widget);
			if (OtherAttribute.IsBound())
			{
				AssignBinding(Widget, MoveTemp(OtherAttribute.Steal().template Get<FGetter>()));
			}
			else if (OtherAttribute.IsSet())
			{
				Set(Widget, MoveTemp(OtherAttribute.Steal().template Get<ObjectType>()));
			}
			else
			{
				Set(Widget, DefaultValue);
			}
		}

		void Assign(SWidget& Widget, const TAttribute<ObjectType>& OtherAttribute, ObjectType&& DefaultValue)
		{
			VerifyOwningWidget(Widget);
			if (OtherAttribute.IsBound())
			{
				AssignBinding(Widget, OtherAttribute.GetBinding());
			}
			else if (OtherAttribute.IsSet())
			{
				Set(Widget, OtherAttribute.Get());
			}
			else
			{
				Set(Widget, MoveTemp(DefaultValue));
			}
		}

		void Assign(SWidget& Widget, TAttribute<ObjectType>&& OtherAttribute, ObjectType&& DefaultValue)
		{
			VerifyOwningWidget(Widget);
			if (OtherAttribute.IsBound())
			{
				AssignBinding(Widget, MoveTemp(OtherAttribute.Steal().template Get<FGetter>()));
			}
			else if (OtherAttribute.IsSet())
			{
				Set(Widget, MoveTemp(OtherAttribute.Steal().template Get<ObjectType>()));
			}
			else
			{
				Set(Widget, MoveTemp(DefaultValue));
			}
		}

		/**
		 * Remove the Getter function.
		 * The Slate Attribute will not be updated anymore and will keep its current cached value.
		 */
		void Unbind(SWidget& Widget)
		{
			VerifyOwningWidget(Widget);
			ProtectedUnregisterAttribute(Widget, InAttributeType);
		}

	public:
		/** Build a Attribute from this SlateAttribute. */
		UE_NODISCARD TAttribute<ObjectType> ToAttribute(const SWidget& Widget) const
		{
			if (ISlateAttributeGetter* Delegate = ProtectedFindGetter(Widget, InAttributeType))
			{
				return TAttribute<ObjectType>::Create(static_cast<FSlateAttributeGetterWrapper<TSlateAttributeBase>*>(Delegate)->GetDelegate());
			}
			return TAttribute<ObjectType>(Get());
		}

		/** @return True if the SlateAttribute is bound to a getter function. */
		UE_NODISCARD bool IsBound(const SWidget& Widget) const
		{
			VerifyOwningWidget(Widget);
			return ProtectedIsBound(Widget, InAttributeType);
		}

		/** @return True if they have the same Getter or the same value. */
		UE_NODISCARD bool IsIdenticalTo(const SWidget& Widget, const TSlateAttributeBase& Other) const
		{
			VerifyOwningWidget(Widget);
			FDelegateHandle ThisDelegateHandle = ProtectedFindGetterHandle(Widget, InAttributeType);
			FDelegateHandle OthersDelegateHandle = ProtectedFindGetterHandle(Widget, InAttributeType);
			if (ThisDelegateHandle == OthersDelegateHandle)
			{
				if (ThisDelegateHandle.IsValid())
				{
					return true;
				}
				return FComparePredicate{}.operator()(Get(), Other.Get());
			}
			return false;
		}

		/** @return True if they have the same Getter or, if the Attribute is set, the same value. */
		UE_NODISCARD bool IsIdenticalTo(const SWidget& Widget, const TAttribute<ObjectType>& Other) const
		{
			VerifyOwningWidget(Widget);
			FDelegateHandle ThisDelegateHandle = ProtectedFindGetterHandle(Widget, InAttributeType);
			if (Other.IsBound())
			{
				return Other.GetBinding().GetHandle() == ThisDelegateHandle;
			}
			return !ThisDelegateHandle.IsValid() && Other.IsSet() && FComparePredicate{}.operator()(Get(), Other.Get());
		}

	private:
		void ConstructWrapper(SWidget& Widget, const FGetter& Getter)
		{
			TUniquePtr<ISlateAttributeGetter> Wrapper = MakeUniqueGetter(*this, Getter);
			ProtectedRegisterAttribute(Widget, InAttributeType, MoveTemp(Wrapper));
			UpdateNowOnBind(Widget);
		}

		void ConstructWrapper(SWidget& Widget, FGetter&& Getter)
		{
			TUniquePtr<ISlateAttributeGetter> Wrapper = MakeUniqueGetter(*this, MoveTemp(Getter));
			ProtectedRegisterAttribute(Widget, InAttributeType, MoveTemp(Wrapper));
			UpdateNowOnBind(Widget);
		}

		void AssignBinding(SWidget& Widget, const FGetter& Getter)
		{
			const FDelegateHandle PreviousGetterHandle = ProtectedFindGetterHandle(Widget, InAttributeType);
			if (PreviousGetterHandle != Getter.GetHandle())
			{
				ConstructWrapper(Widget, Getter);
			}
		}

		void AssignBinding(SWidget& Widget, FGetter&& Getter)
		{
			const FDelegateHandle PreviousGetterHandle = ProtectedFindGetterHandle(Widget, InAttributeType);
			if (PreviousGetterHandle != Getter.GetHandle())
			{
				ConstructWrapper(Widget, MoveTemp(Getter));
			}
		}

	private:
		template<typename SlateAttributeType>
		class FSlateAttributeGetterWrapper : public ISlateAttributeGetter
		{
		public:
			using ObjectType = typename SlateAttributeType::ObjectType;
			using FGetter = typename TAttribute<ObjectType>::FGetter;
			using FComparePredicate = typename SlateAttributeType::FComparePredicate;

			FSlateAttributeGetterWrapper() = delete;
			FSlateAttributeGetterWrapper(const FSlateAttributeGetterWrapper&) = delete;
			FSlateAttributeGetterWrapper& operator= (const FSlateAttributeGetterWrapper&) = delete;
			virtual ~FSlateAttributeGetterWrapper() = default;

		public:
			FSlateAttributeGetterWrapper(SlateAttributeType& InOwningAttribute, const FGetter& InGetterDelegate)
				: Getter(InGetterDelegate)
				, Attribute(&InOwningAttribute)
			{
			}
			FSlateAttributeGetterWrapper(SlateAttributeType& InOwningAttribute, FGetter&& InGetterDelegate)
				: Getter(MoveTemp(InGetterDelegate))
				, Attribute(&InOwningAttribute)
			{
			}

			virtual FUpdateAttributeResult UpdateAttribute(const SWidget& Widget) override
			{
				ObjectType NewValue = Getter.Execute();

				if (!(FComparePredicate{}.operator()(Attribute->Value, NewValue)))
				{
					// Set the value on the widget
					Attribute->Value = MoveTemp(NewValue);
					return Attribute->GetInvalidationReason(Widget);
				}
				return FUpdateAttributeResult();
			}

			virtual const FSlateAttributeBase& GetAttribute() const override
			{
				return *Attribute;
			}

			virtual void SetAttribute(FSlateAttributeBase& InBase) override
			{
				Attribute = &static_cast<SlateAttributeType&>(InBase);
			}

			virtual FDelegateHandle GetDelegateHandle() const override
			{
				return Getter.GetHandle();
			}

			const FGetter& GetDelegate() const
			{
				return Getter;
			}

		private:
			/** Getter function to fetch the new value of the SlateAttribute. */
			FGetter Getter;
			/** The SlateAttribute of the SWidget owning the value. */
			SlateAttributeType* Attribute;
		};

		static TUniquePtr<ISlateAttributeGetter> MakeUniqueGetter(TSlateAttributeBase& Attribute, const FGetter& Getter)
		{
			return MakeUnique<FSlateAttributeGetterWrapper<TSlateAttributeBase>>(Attribute, Getter);
		}

		static TUniquePtr<ISlateAttributeGetter> MakeUniqueGetter(TSlateAttributeBase& Attribute, FGetter&& Getter)
		{
			return MakeUnique<FSlateAttributeGetterWrapper<TSlateAttributeBase>>(Attribute, MoveTemp(Getter));
		}

	private:
		ObjectType Value;

	protected:
#if UE_SLATE_WITH_MEMBER_ATTRIBUTE_DEBUGGING
		SWidget* Debug_OwningWidget;
#endif
	};


	/**
	 *
	 */
	template<typename InObjectType, typename InInvalidationReasonPredicate, typename InComparePredicate = TEqualTo<>>
	struct TSlateMemberAttribute : public TSlateAttributeBase<InObjectType, InInvalidationReasonPredicate, InComparePredicate, ESlateAttributeType::Member>
	{
	private:
		using Super = TSlateAttributeBase<InObjectType, InInvalidationReasonPredicate, InComparePredicate, ESlateAttributeType::Member>;

		template<typename WidgetType, typename U = typename std::enable_if<std::is_base_of<SWidget, WidgetType>::value>::type>
		static void VerifyAttributeAddress(WidgetType& Widget, TSlateMemberAttribute* Self)
		{
			checkf((UPTRINT)Self >= (UPTRINT)&Widget && (UPTRINT)Self < (UPTRINT)&Widget + sizeof(WidgetType),
				TEXT("Use TAttribute or TSlateManagedAttribute instead. See SlateAttribute.h for more info."));
			ensureAlwaysMsgf((HasDefinedInvalidationReason || Self->ProtectedIsImplemented(Widget)),
				TEXT("The TSlateAttribute could not be found in the SlateAttributeDescriptor.\n")
				TEXT("Use the SLATE_DECLARE_WIDGET and add the attribute in PrivateRegisterAttributes,\n")
				TEXT("Or use TSlateAttribute with a valid Invalidation Reason instead."));
		}

	public:
		using FGetter = typename Super::FGetter;
		using ObjectType = typename Super::ObjectType;
		static const bool IsMemberType = true;
		static constexpr bool HasDefinedInvalidationReason = !std::is_same<InInvalidationReasonPredicate, FSlateAttributeNoInvalidationReason>::value;

	public:
		//~ You can only register Attribute that are defined in a SWidget.
		//~ Use the constructor with Widget pointer.
		//~ See documentation on top of this file for more info.
		TSlateMemberAttribute() = delete;
		TSlateMemberAttribute(const TSlateMemberAttribute&) = delete;
		TSlateMemberAttribute(TSlateMemberAttribute&&) = delete;
		TSlateMemberAttribute& operator=(const TSlateMemberAttribute&) = delete;
		TSlateMemberAttribute& operator=(TSlateMemberAttribute&&) = delete;

#if UE_SLATE_WITH_MEMBER_ATTRIBUTE_DEBUGGING
		~TSlateMemberAttribute()
		{
			/**
			 * The parent should now be destroyed and the shared pointer should invalidate.
			 * If you hit the check, that means the TSlateAttribute is not a variable member of SWidget.
			 * It will introduced bad memory access.
			 * See documentation above.
			*/
			checkf(Super::ProtectedIsWidgetInDestructionPath(Super::Debug_OwningWidget), TEXT("The Owning widget should be invalid."));
		}
#endif

		template<typename WidgetType, typename U = typename std::enable_if<std::is_base_of<SWidget, WidgetType>::value>::type>
		explicit TSlateMemberAttribute(WidgetType& Widget)
			: Super(Widget)
		{
			VerifyAttributeAddress(Widget, this);
		}

		template<typename WidgetType, typename U = typename std::enable_if<std::is_base_of<SWidget, WidgetType>::value>::type>
		explicit TSlateMemberAttribute(WidgetType& Widget, const ObjectType& InValue)
			: Super(Widget, InValue)
		{
			VerifyAttributeAddress(Widget, this);
		}

		template<typename WidgetType, typename U = typename std::enable_if<std::is_base_of<SWidget, WidgetType>::value>::type>
		explicit TSlateMemberAttribute(WidgetType& Widget, ObjectType&& InValue)
			: Super(Widget, MoveTemp(InValue))
		{
			VerifyAttributeAddress(Widget, this);
		}
	};


	/**
	 *
	 */
	template<typename InObjectType, typename InInvalidationReasonPredicate, typename InComparePredicate = TEqualTo<>>
	struct TSlateManagedAttribute : protected TSlateAttributeBase<InObjectType, InInvalidationReasonPredicate, InComparePredicate, ESlateAttributeType::Managed>
	{
	private:
		using Super = TSlateAttributeBase<InObjectType, InInvalidationReasonPredicate, InComparePredicate, ESlateAttributeType::Managed>;

	public:
		using ObjectType = typename Super::ObjectType;
		using FInvalidationReasonPredicate = typename Super::FInvalidationReasonPredicate;
		using FGetter = typename Super::FGetter;
		using FComparePredicate = typename Super::FComparePredicate;
		static const bool IsMemberType = false;

		static EInvalidateWidgetReason GetInvalidationReason(const SWidget& Widget) { return Super::GetInvalidationReason(Widget); }

	public:
		TSlateManagedAttribute() = delete;
		TSlateManagedAttribute(const TSlateManagedAttribute&) = delete;
		TSlateManagedAttribute& operator=(const TSlateManagedAttribute&) = delete;

		~TSlateManagedAttribute()
		{
			Unbind();
		}

		TSlateManagedAttribute(TSlateManagedAttribute&& Other)
			: Super(MoveTemp(Other))
			, ManagedWidget(MoveTemp(Other.ManagedWidget))
		{
			if (TSharedPtr<SWidget> Pin = ManagedWidget.Pin())
			{
				FSlateAttributeImpl::ProtectedMoveAttribute(*Pin.Get(), ESlateAttributeType::Managed, &Other);
			}
		}

		TSlateManagedAttribute& operator=(TSlateManagedAttribute&& Other)
		{
			Super::operator=(MoveTemp(Other));
			ManagedWidget = MoveTemp(Other.ManagedWidget);

			if (TSharedPtr<SWidget> Pin = ManagedWidget.Pin())
			{
				FSlateAttributeImpl::ProtectedMoveAttribute(*Pin.Get(), ESlateAttributeType::Managed, &Other);
			}
			return *this;
		}

		explicit TSlateManagedAttribute(TSharedRef<SWidget> Widget)
			: Super(Widget.Get())
			, ManagedWidget(Widget)
		{ }

		explicit TSlateManagedAttribute(TSharedRef<SWidget> Widget, const ObjectType& InValue)
			: Super(Widget.Get(), InValue)
			, ManagedWidget(Widget)
		{
		}

		explicit TSlateManagedAttribute(TSharedRef<SWidget> Widget, ObjectType&& InValue)
			: Super(Widget.Get(), MoveTemp(InValue))
			, ManagedWidget(Widget)
		{
		}

		explicit TSlateManagedAttribute(TSharedRef<SWidget> Widget, const FGetter& Getter, const ObjectType& InitialValue)
			: Super(Widget.Get(), Getter, InitialValue)
			, ManagedWidget(Widget)
		{
		}

		explicit TSlateManagedAttribute(TSharedRef<SWidget> Widget, const FGetter& Getter, ObjectType&& InitialValue)
			: Super(Widget.Get(), Getter, MoveTemp(InitialValue))
			, ManagedWidget(Widget)
		{
		}

		explicit TSlateManagedAttribute(TSharedRef<SWidget> Widget, FGetter&& Getter, const ObjectType& InitialValue)
			: Super(Widget.Get(), MoveTemp(Getter), InitialValue)
			, ManagedWidget(Widget)
		{
		}

		explicit TSlateManagedAttribute(TSharedRef<SWidget> Widget, FGetter&& Getter, ObjectType&& InitialValue)
			: Super(Widget.Get(), MoveTemp(Getter), MoveTemp(InitialValue))
			, ManagedWidget(Widget)
		{
		}

		explicit TSlateManagedAttribute(TSharedRef<SWidget> Widget, const TAttribute<ObjectType>& Attribute, const ObjectType& InitialValue)
			: Super(Widget.Get(), Attribute, InitialValue)
			, ManagedWidget(Widget)
		{
		}

		explicit TSlateManagedAttribute(TSharedRef<SWidget> Widget, TAttribute<ObjectType>&& Attribute, ObjectType&& InitialValue)
			: Super(Widget.Get(), MoveTemp(Attribute), MoveTemp(InitialValue))
			, ManagedWidget(Widget)
		{
		}

	public:
		const ObjectType& Get() const
		{
			return Super::Get();
		}

		void UpdateNow()
		{
			if (TSharedPtr<SWidget> Pin = ManagedWidget.Pin())
			{
				Super::UpdateNow(*Pin.Get());
			}
		}

	public:
		void Set(const ObjectType& NewValue)
		{
			if (TSharedPtr<SWidget> Pin = ManagedWidget.Pin())
			{
				Super::Set(*Pin.Get(), NewValue);
			}
		}

		void Set(ObjectType&& NewValue)
		{
			if (TSharedPtr<SWidget> Pin = ManagedWidget.Pin())
			{
				Super::Set(*Pin.Get(), MoveTemp(NewValue));
			}
		}

		void Bind(const FGetter& Getter)
		{
			if (TSharedPtr<SWidget> Pin = ManagedWidget.Pin())
			{
				Super::Bind(*Pin.Get(), Getter);
			}
		}

		void Bind(FGetter&& Getter)
		{
			if (TSharedPtr<SWidget> Pin = ManagedWidget.Pin())
			{
				Super::Bind(*Pin.Get(), MoveTemp(Getter));
			}
		}

		void Assign(const TAttribute<ObjectType>& OtherAttribute)
		{
			if (TSharedPtr<SWidget> Pin = ManagedWidget.Pin())
			{
				Super::Assign(*Pin.Get(), OtherAttribute);
			}
		}

		void Assign(TAttribute<ObjectType>&& OtherAttribute)
		{
			if (TSharedPtr<SWidget> Pin = ManagedWidget.Pin())
			{
				Super::Assign(*Pin.Get(), MoveTemp(OtherAttribute));
			}
		}

		void Assign(const TAttribute<ObjectType>& OtherAttribute, const ObjectType& DefaultValue)
		{
			if (TSharedPtr<SWidget> Pin = ManagedWidget.Pin())
			{
				Super::Assign(*Pin.Get(), OtherAttribute, DefaultValue);
			}
		}

		void Assign(const TAttribute<ObjectType>& OtherAttribute, ObjectType&& DefaultValue)
		{
			if (TSharedPtr<SWidget> Pin = ManagedWidget.Pin())
			{
				Super::Assign(*Pin.Get(), OtherAttribute, MoveTemp(DefaultValue));
			}
		}

		void Assign(TAttribute<ObjectType>&& OtherAttribute, const ObjectType& DefaultValue)
		{
			if (TSharedPtr<SWidget> Pin = ManagedWidget.Pin())
			{
				Super::Assign(*Pin.Get(), MoveTemp(OtherAttribute), DefaultValue);
			}
		}

		void Assign(TAttribute<ObjectType>&& OtherAttribute, ObjectType&& DefaultValue)
		{
			if (TSharedPtr<SWidget> Pin = ManagedWidget.Pin())
			{
				Super::Assign(*Pin.Get(), MoveTemp(OtherAttribute), MoveTemp(DefaultValue));
			}
		}

		void Unbind()
		{
			if (TSharedPtr<SWidget> Pin = ManagedWidget.Pin())
			{
				Super::Unbind(*Pin.Get());
			}
		}

	public:
		bool IsBound() const
		{
			if (TSharedPtr<SWidget> Pin = ManagedWidget.Pin())
			{
				return Super::IsBound(*Pin.Get());
			}
			return false;
		}

		bool IsIdenticalTo(const TSlateManagedAttribute& Other) const
		{
			if (TSharedPtr<SWidget> Pin = ManagedWidget.Pin())
			{
				return Super::IsIdenticalTo(*Pin.Get(), Other);
			}
			return false;
		}

		bool IsIdenticalTo(const TAttribute<ObjectType>& Other) const
		{
			if (TSharedPtr<SWidget> Pin = ManagedWidget.Pin())
			{
				return Super::IsIdenticalTo(*Pin.Get(), Other);
			}
			return false;
		}

	private:
		TWeakPtr<SWidget> ManagedWidget;
	};


	/*
	 * A reference to a SlateAttribute that can be returned and saved for later.
	 */
	template<typename AttributeMemberType>
	struct TSlateMemberAttributeRef
	{
	public:
		using SlateAttributeType = AttributeMemberType;
		using ObjectType = typename AttributeMemberType::ObjectType;
		using AttributeType = TAttribute<ObjectType>;

	private:
		template<typename WidgetType>
		static void VerifyAttributeAddress(WidgetType const& InWidget, AttributeMemberType const& InAttribute)
		{
			checkf((UPTRINT)&InAttribute >= (UPTRINT)&InWidget && (UPTRINT)&InAttribute < (UPTRINT)&InWidget + sizeof(WidgetType),
				TEXT("The attribute is not a member of the widget."));
			InAttribute.VerifyOwningWidget(InWidget);
		}

	public:
		/** Constructor */
		TSlateMemberAttributeRef() = default;

		template<typename WidgetType, typename V = typename std::enable_if<std::is_base_of<SWidget, WidgetType>::value>::type>
		explicit TSlateMemberAttributeRef(WidgetType const& InOwner, AttributeMemberType const& InAttribute)
			: Owner(InOwner.AsShared())
			, Attribute(&InAttribute)
		{
			VerifyAttributeAddress(InOwner, InAttribute);
		}

	public:
		/** @return if the reference is valid. A reference can be invalid if the SWidget is destroyed. */
		UE_NODISCARD bool IsValid() const
		{
			return Owner.IsValid();
		}

		/** @return the SlateAttribute cached value; undefined when IsValid() returns false. */
		UE_NODISCARD const ObjectType& Get() const
		{
			if (TSharedPtr<const SWidget> Pin = Owner.Pin())
			{
				return Attribute->Get();
			}
			checkf(false, TEXT("It is an error to call GetValue() on an unset TSlateMemberAttributeRef. Please either check IsValid() or use Get(DefaultValue) instead."));
			static ObjectType Tmp;
			return Tmp;
		}

		/** @return the SlateAttribute cached value or the DefaultValue if the reference is invalid. */
		UE_NODISCARD const ObjectType& Get(const ObjectType& DefaultValue) const
		{
			if (TSharedPtr<const SWidget> Pin = Owner.Pin())
			{
				return Attribute->Get();
			}
			return DefaultValue;
		}

		/** Update the cached value and invalidate the widget if needed. */
		void UpdateValue()
		{
			if (TSharedPtr<const SWidget> Pin = Owner.Pin())
			{
				const_cast<AttributeMemberType*>(Attribute)->UpdateNow(*Pin.Get());
			}
		}

		/**
		 * Assumes the reference is valid.
		 * Shorthand for the boilerplace code: MyAttribute.UpdateValueNow(); MyAttribute.Get();
		 */
		UE_NODISCARD const ObjectType& UpdateAndGet()
		{
			if (TSharedPtr<const SWidget> Pin = Owner.Pin())
			{
				const_cast<AttributeMemberType*>(Attribute)->UpdateNow(*Pin.Get());
				return Attribute->Get();
			}
			checkf(false, TEXT("It is an error to call GetValue() on an unset TSlateMemberAttributeRef. Please either check IsValid() or use Get(DefaultValue) instead."));
		}

		/**
		 * Shorthand for the boilerplace code: MyAttribute.UpdateValueNow(); MyAttribute.Get(DefaultValue);
		 * @return the SlateAttribute cached value or the DefaultValue if the reference is invalid.
		 */
		UE_NODISCARD const ObjectType& UpdateAndGet(const ObjectType& DefaultValue)
		{
			if (TSharedPtr<const SWidget> Pin = Owner.Pin())
			{
				const_cast<AttributeMemberType*>(Attribute)->UpdateNow(*Pin.Get());
				return Attribute->Get();
			}
			return DefaultValue;
		}

		/** Build a Attribute from this SlateAttribute. */
		UE_NODISCARD TAttribute<ObjectType> ToAttribute() const
		{
			if (TSharedPtr<const SWidget> Pin = Owner.Pin())
			{
				return Attribute->ToAttribute(*Pin.Get());
			}
			return TAttribute<ObjectType>();
		}

		/** @return True if the SlateAttribute is bound to a getter function. */
		UE_NODISCARD bool IsBound() const
		{
			if (TSharedPtr<const SWidget> Pin = Owner.Pin())
			{
				return Attribute->IsBound(*Pin.Get());
			}
			return false;
		}

		/** @return True if they have the same Getter or the same value. */
		UE_NODISCARD bool IsIdenticalTo(const TSlateMemberAttributeRef& Other) const
		{
			TSharedPtr<const SWidget> SelfPin = Owner.Pin();
			TSharedPtr<const SWidget> OtherPin = Other.Owner.Pin();
			if (SelfPin == OtherPin && SelfPin)
			{
				return Attribute->IsIdenticalTo(*SelfPin.Get(), *Other.Attribute);
			}
			return SelfPin == OtherPin;
		}

		/** @return True if they have the same Getter or, if the Attribute is set, the same value. */
		UE_NODISCARD bool IsIdenticalTo(const TAttribute<ObjectType>& Other) const
		{
			if (TSharedPtr<const SWidget> Pin = Owner.Pin())
			{
				return Attribute->IsIdenticalTo(*Pin.Get(), Other);
			}
			return !Other.IsSet(); // if the other is not set, then both are invalid.
		}

	private:
		TWeakPtr<const SWidget> Owner;
		AttributeMemberType const* Attribute = nullptr;
	 };


} // SlateAttributePrivate
