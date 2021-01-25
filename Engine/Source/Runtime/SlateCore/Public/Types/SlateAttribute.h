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

/**
 * Use TSlateAttribute when it's a SWidget member.
 * Use TSlateManagedAttribute when it's a member inside an array or other moving structure, and the array is a SWidget member. They can only be moved (they can't be copied).
 * For everything else, use TAttribute.
 *
 *
 *
 * In Slate TAttributes are optimized for developer efficiency.
 * They enable widgets to poll for data instead of requiring the user to manually set the state on widgets.
 * Attributes generally work well when performance is not a concern but break down when performance is critical (like a game UI).
 *
 * The invalidation system allows only widgets that have changed to perform expensive Slate layout.
 * Bound TAttributes are incompatible with invalidation because we do not know when the data changes.
 * Additionally TAttributes for common functionality such as visibility are called multiple times per frame and the delegate overhead for that alone is very high.
 * TAttributes have a high memory overhead and are not cache-friendly.
 *
 * TSlateAttribute makes the attribute system viable for invalidation and more performance-friendly while keeping the benefits of attributes intact.
 * TSlateAttributes are updated once per frame before the Prepass. If the cached value of the TSlateAttribute changes, then it will invalidate the widget.
 * The TSlateAttributes are updated in the order they are defined in the SWidget (by default). TSlateManagedAttribute are updated in a random order (after TSlateAttribute).
 * The order can be defined/override by setting a Prerequisite (see bellow for an example).
 * The invalidation reason can be a predicate (see bellow for an example).
 * The invalidation reason can be override per SWidget. Use override with precaution since it can break the invalidation of child widget.
 *
 *
 * TSlateAttribute is not copyable and can only live inside a SWidget.
 * For performance reasons, we do not save the extra information that would be needed to be memory save in all contexts.
 * If you create a TSlateAttribute that can be moved, you need to use TSlateManagedAttribute.
 * The managed version is as fast but use more memory and is less cache-friendly.
 * Note, if you use TAttribute to change the state of a SWidget, you need to override bool ComputeVolatility() const.
 *
 * TSlateAttribute request a SWidget pointer. The "this" pointer should ALWAYS be used.
 * (The SlateAttribute pointer is saved inside the SlateAttributeMetaData. The widget needs to be aware when the pointer changes.)
 *
 *
 *	.h
 *  // The new way of using attribute in your SWidget
 *	class SMyNewWidget : public SLeafWidget
 *	{
 *		SLATE_DECLARE_WIDGET(SMyNewWidget, SLeafWidget)	// I is optional.
														// If added, you need to implement void PrivateRegisterAttributes(FSlateAttributeInitializer& AttributeInitializer).
 *
 *		SLATE_BEGIN_ARGS(SMyNewWidget)
 *			SLATE_ATTRIBUTE(FLinearColor, Color)
 *		SLATE_END_ARGS()
 *
 *		SMyNewWidget()
 *			: NewWay(*this, FLinearColor::White) // Always use "this" pointer. No exception. If you can't, use TAttribute.
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
			// If the TAttribute is bound, bound the TSlateAttribute
			// It the TAttribute is set, set the TSlateAttribute, test if the value changed and invalidate the widget.
 *			// Else, nothing (use the previous value).
 *			NewWays.Bind(*this, InArgs._Color); // Always use the "this" pointer.
 *		}
 *
 *		// If NewWay is bounded and the values changed from the previous frame, the widget will be invalidated.
 *		//In this case, it will only paint the widget (no layout required).
 *		TSlateAttribute<FLinearColor, EInvalidationReason::Paint> NewWay;
 *	};
 *
 *  .cpp
 *	// This is optional. It is still a good practice to implement it since it will allow user to override the default behavior and control the update order.
 *	// The WidgetReflector use that information.
 *	SLATE_IMPLEMENT_WIDGET(STextBlock)
 *	void STextBlock::PrivateRegisterAttributes(FSlateAttributeInitializer& AttributeInitializer)
 *	{
 *		SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION(AttributeInitializer, BoundText)
			.SetPrerequisite("bEnabled"); // SetPrerequisite is not needed here, this is just an example to show how you could do it if needed.

		//bEnabled invalidate paint, we need it to invalidate the Layout.
		AttributeInitializer.OverrideInvalidationReason("bEnabled", EWidgetInvalidationReason::Layout | EWidgetInvalidationReason::Paint);
 *	}
 *
 *
 * // We used to do this. Keep using this method when you are not able to provide the "this" pointer to TSlateAttribute.
 * class SMyOldWidget : public SLeafWidget
 * {
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

/** */
struct TSlateAttributeFTextComparePredicate
{
	bool operator()(const FText& Lhs, const FText& Rhs) const
	{
		return Lhs.IdenticalTo(Rhs, ETextIdenticalModeFlags::DeepCompare | ETextIdenticalModeFlags::LexicalCompareInvariants);
	}
};

/** */
struct FSlateAttributeBase
{
	
};

/** */
template<EInvalidateWidgetReason InvalidationReason>
struct TSlateAttributeInvalidationReason
{
	static constexpr EInvalidateWidgetReason GetInvalidationReason(const SWidget&) { return InvalidationReason; }
};

/** */
namespace SlateAttributePrivate
{
	/** */
	enum class ESlateAttributeType : uint8
	{
		Member = 0,
		Managed = 1,
	};

	class ISlateAttributeGetter;
	template<typename ObjectType, typename InvalidationReasonPredicate, typename FComparePredicate, ESlateAttributeType AttributeType>
	struct TSlateAttributeBase;


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
		void ProtectedUnregisterAttribute(SWidget& Widget, ESlateAttributeType AttributeType) const;
		void ProtectedRegisterAttribute(SWidget& Widget, ESlateAttributeType AttributeType, TUniquePtr<ISlateAttributeGetter>&& Wrapper);
		void ProtectedInvalidateWidget(SWidget& Widget, ESlateAttributeType AttributeType, EInvalidateWidgetReason InvalidationReason) const;
		bool ProtectedIsBound(const SWidget& Widget, ESlateAttributeType AttributeType) const;
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
		using ObjectType = InObjectType;
		using FInvalidationReasonPredicate = InInvalidationReasonPredicate;
		using FGetter = typename TAttribute<ObjectType>::FGetter;
		using FComparePredicate = InComparePredicateType;

		static EInvalidateWidgetReason GetInvalidationReason(const SWidget& Widget) { return FInvalidationReasonPredicate::GetInvalidationReason(Widget); }
		
	private:
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

		TSlateAttributeBase(SWidget& Widget, const ObjectType& InitialValue, const FGetter& Getter)
			: Value(InitialValue)
#if UE_SLATE_WITH_MEMBER_ATTRIBUTE_DEBUGGING
			, Debug_OwningWidget(&Widget)
#endif
		{
			if (Getter.IsBound())
			{
				TUniquePtr<ISlateAttributeGetter> Wrapper = MakeUniqueGetter(*this, Getter);
				ProtectedRegisterAttribute(Widget, InAttributeType, MoveTemp(Wrapper));
			}
		}

		TSlateAttributeBase(SWidget& Widget, const ObjectType& InitialValue, FGetter&& Getter)
			: Value(InitialValue)
#if UE_SLATE_WITH_MEMBER_ATTRIBUTE_DEBUGGING
			, Debug_OwningWidget(&Widget)
#endif
		{
			if (Getter.IsBound())
			{
				TUniquePtr<ISlateAttributeGetter> Wrapper = MakeUniqueGetter(*this, MoveTemp(Getter));
				ProtectedRegisterAttribute(Widget, InAttributeType, MoveTemp(Wrapper));
			}
		}

		TSlateAttributeBase(SWidget& Widget, const ObjectType& InitialValue, const TAttribute<ObjectType>& Attribute)
			: Value((Attribute.IsSet() && !Attribute.IsBound()) ? Attribute.Get() : InitialValue)
#if UE_SLATE_WITH_MEMBER_ATTRIBUTE_DEBUGGING
			, Debug_OwningWidget(&Widget)
#endif
		{
			if (Attribute.IsBound())
			{
				TUniquePtr<ISlateAttributeGetter> Wrapper = MakeUniqueGetter(*this, Attribute.GetBinding());
				ProtectedRegisterAttribute(Widget, InAttributeType, MoveTemp(Wrapper));
			}
		}

		TSlateAttributeBase(SWidget& Widget, ObjectType&& InitialValue, const FGetter& Getter)
			: Value(MoveTemp(InitialValue))
#if UE_SLATE_WITH_MEMBER_ATTRIBUTE_DEBUGGING
			, Debug_OwningWidget(&Widget)
#endif
		{
			if (Getter.IsBound())
			{
				TUniquePtr<ISlateAttributeGetter> Wrapper = MakeUniqueGetter(*this, Getter);
				ProtectedRegisterAttribute(Widget, InAttributeType, MoveTemp(Wrapper));
			}
		}

		TSlateAttributeBase(SWidget& Widget, ObjectType&& InitialValue, FGetter&& Getter)
			: Value(MoveTemp(InitialValue))
#if UE_SLATE_WITH_MEMBER_ATTRIBUTE_DEBUGGING
			, Debug_OwningWidget(&Widget)
#endif
		{
			if (Getter.IsBound())
			{
				TUniquePtr<ISlateAttributeGetter> Wrapper = MakeUniqueGetter(*this, MoveTemp(Getter));
				ProtectedRegisterAttribute(Widget, InAttributeType, MoveTemp(Wrapper));
			}
		}

		TSlateAttributeBase(SWidget& Widget, ObjectType&& InitialValue, const TAttribute<ObjectType>& Attribute)
			: Value((Attribute.IsSet() && !Attribute.IsBound()) ? Attribute.Get() : MoveTemp(InitialValue))
#if UE_SLATE_WITH_MEMBER_ATTRIBUTE_DEBUGGING
			, Debug_OwningWidget(&Widget)
#endif
		{
			if (Attribute.IsBound())
			{
				TUniquePtr<ISlateAttributeGetter> Wrapper = MakeUniqueGetter(*this, Attribute.GetBinding());
				ProtectedRegisterAttribute(Widget, InAttributeType, MoveTemp(Wrapper));
			}
		}

	public:
		/** @return the SlateAttribute value. If the SlateAttribute is bound, value is cached only at the end of the frame. */ 
		const ObjectType& Get() const
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

		/** Bind the SlateAttribute to the Getter function. The SlateAttribute will be updated every frame from the Getter. */
		void Bind(SWidget& Widget, const FGetter& Getter)
		{
			VerifyOwningWidget(Widget);
			if (Getter.IsBound())
			{
				const FDelegateHandle PreviousGetterHandle = ProtectedFindGetterHandle(Widget, InAttributeType);
				if (PreviousGetterHandle != Getter.GetHandle())
				{
					TUniquePtr<ISlateAttributeGetter> Wrapper = MakeUniqueGetter(*this, Getter);
					ProtectedRegisterAttribute(Widget, InAttributeType, MoveTemp(Wrapper));
				}
			}
			else
			{
				ProtectedUnregisterAttribute(Widget, InAttributeType);
			}
		}

		/** Bind the SlateAttribute to the Getter function. The SlateAttribute will be updated every frame from the Getter. */
		void Bind(SWidget& Widget, FGetter&& Getter)
		{
			VerifyOwningWidget(Widget);
			if (Getter.IsBound())
			{
				const FDelegateHandle PreviousGetterHandle = ProtectedFindGetterHandle(Widget, InAttributeType);
				if (PreviousGetterHandle != Getter.GetHandle())
				{
					TUniquePtr<ISlateAttributeGetter> Wrapper = MakeUniqueGetter(*this, MoveTemp(Getter));
					ProtectedRegisterAttribute(Widget, InAttributeType, MoveTemp(Wrapper));
				}
			}
			else
			{
				ProtectedUnregisterAttribute(Widget, InAttributeType);
			}
		}

		/** Bind the SlateAttribute to the Getter function. The SlateAttribute will be updated every frame from the Getter. */
		template<typename WidgetType, typename U = typename std::enable_if<std::is_base_of<SWidget, WidgetType>::value>::type>
		void Bind(WidgetType& Widget, typename FGetter::template TSPMethodDelegate_Const<WidgetType>::FMethodPtr MethodPtr)
		{
			Bind(Widget, FGetter::CreateSP(&Widget, MethodPtr));
		}

		/**
		 * Bind the SlateAttribute to the Attribute Getter function (if it exist). The SlateAttribute will be updated every frame from the Getter.
		 * Or set the value if the Attribute is bound.
		 * This will Unbind any previously bound getter.
		 * It may Invalidate the Widget. @see Set
		 */
		void Assign(SWidget& Widget, const TAttribute<ObjectType>& OtherAttribute)
		{
			VerifyOwningWidget(Widget);
			if (OtherAttribute.IsBound())
			{
				const FDelegateHandle PreviousGetterHandle = ProtectedFindGetterHandle(Widget, InAttributeType);
				if (PreviousGetterHandle != OtherAttribute.GetBinding().GetHandle())
				{
					TUniquePtr<ISlateAttributeGetter> Wrapper = MakeUniqueGetter(*this, OtherAttribute.GetBinding());
					ProtectedRegisterAttribute(Widget, InAttributeType, MoveTemp(Wrapper));
				}
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

		/**
		 * Bind the SlateAttribute to the Attribute Getter function (if it exist). The SlateAttribute will be updated every frame from the Getter.
		 * Or set the value if the Attribute is bound. If the Attribute is not set the DefaultValue will be used.
		 * This will Unbind any previously bound getter.
		 * It may Invalidate the Widget. @see Set
		 */
		void Assign(SWidget& Widget, const TAttribute<ObjectType>& OtherAttribute, const ObjectType& DefaultValue)
		{
			VerifyOwningWidget(Widget);
			if (OtherAttribute.IsBound())
			{
				const FDelegateHandle PreviousGetterHandle = ProtectedFindGetterHandle(Widget, InAttributeType);
				if (PreviousGetterHandle != OtherAttribute.GetBinding().GetHandle())
				{
					TUniquePtr<ISlateAttributeGetter> Wrapper = MakeUniqueGetter(*this, OtherAttribute.GetBinding());
					ProtectedRegisterAttribute(Widget, InAttributeType, MoveTemp(Wrapper));
				}
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

		/**
		 * Bind the SlateAttribute to the Attribute Getter function (if it exist). The SlateAttribute will be updated every frame from the Getter.
		 * Or set the value if the Attribute is bound. If the Attribute is not set the DefaultValue will be used.
		 * This will Unbind any previously bound getter.
		 * It may Invalidate the Widget. @see Set
		 */
		void Assign(SWidget& Widget, const TAttribute<ObjectType>& OtherAttribute, ObjectType&& DefaultValue)
		{
			VerifyOwningWidget(Widget);
			if (OtherAttribute.IsBound())
			{
				const FDelegateHandle PreviousGetterHandle = ProtectedFindGetterHandle(Widget, InAttributeType);
				if (PreviousGetterHandle != OtherAttribute.GetBinding().GetHandle())
				{
					TUniquePtr<ISlateAttributeGetter> Wrapper = MakeUniqueGetter(*this, OtherAttribute.GetBinding());
					ProtectedRegisterAttribute(Widget, InAttributeType, MoveTemp(Wrapper));
				}
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
		/** @return True if the SlateAttribute is bound to a getter function. */
		bool IsBound(const SWidget& Widget) const
		{
			VerifyOwningWidget(Widget);
			return ProtectedIsBound(Widget, InAttributeType);
		}

		/** @return True if they have the same Getter or the same value. */
		bool IsIdenticalTo(const SWidget& Widget, const TSlateAttributeBase& Other) const
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
				return Get() == Other.Get();
			}
			return false;
		}

		/**  if they have the same Getter or, if the Attribute is set, the same value. */
		bool IsIdenticalTo(const SWidget& Widget, const TAttribute<ObjectType>& Other) const
		{
			VerifyOwningWidget(Widget);
			FDelegateHandle ThisDelegateHandle = ProtectedFindGetterHandle(Widget, InAttributeType);
			if (Other.IsBound())
			{
				return Other.GetBinding().GetHandle() == ThisDelegateHandle;
			}
			return !ThisDelegateHandle.IsValid() && Other.IsSet() && Get() == Other.Get();
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

		//using TSlateAttributeBase<InObjectType, InInvalidationReasonPredicate, InComparePredicate, ESlateAttributeType::Member>::TSlateAttributeBase;

		template<typename WidgetType, typename U = typename std::enable_if<std::is_base_of<SWidget, WidgetType>::value>::type>
		static void VerifyAttributeAddress(WidgetType& Widget, TSlateMemberAttribute* Self)
		{
			checkf((UPTRINT)Self >= (UPTRINT)&Widget && (UPTRINT)Self < (UPTRINT)&Widget + sizeof(WidgetType),
				TEXT("Use TAttribute or TSlateManagedAttribute instead. See SlateAttribute.h for more info."));
		}

	public:
		using FGetter = typename Super::FGetter;
		using ObjectType = typename Super::ObjectType;

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
		TSlateMemberAttribute(WidgetType& Widget)
			: Super(Widget)
		{
			VerifyAttributeAddress(Widget, this);
		}

		template<typename WidgetType, typename U = typename std::enable_if<std::is_base_of<SWidget, WidgetType>::value>::type>
		TSlateMemberAttribute(WidgetType& Widget, const ObjectType& InValue)
			: Super(Widget, InValue)
		{
			VerifyAttributeAddress(Widget, this);
		}

		template<typename WidgetType, typename U = typename std::enable_if<std::is_base_of<SWidget, WidgetType>::value>::type>
		TSlateMemberAttribute(WidgetType& Widget, ObjectType&& InValue)
			: Super(Widget, MoveTemp(InValue))
		{
			VerifyAttributeAddress(Widget, this);
		}

		template<typename WidgetType, typename U = typename std::enable_if<std::is_base_of<SWidget, WidgetType>::value>::type>
		TSlateMemberAttribute(WidgetType& Widget, const ObjectType& InitialValue, const FGetter& Getter)
			: Super(Widget, InitialValue, Getter)
		{
			VerifyAttributeAddress(Widget, this);
		}

		template<typename WidgetType, typename U = typename std::enable_if<std::is_base_of<SWidget, WidgetType>::value>::type>
		TSlateMemberAttribute(WidgetType& Widget, const ObjectType& InitialValue, FGetter&& Getter)
			: Super(Widget, InitialValue, MoveTemp(Getter))
		{
			VerifyAttributeAddress(Widget, this);
		}

		template<typename WidgetType, typename U = typename std::enable_if<std::is_base_of<SWidget, WidgetType>::value>::type>
		TSlateMemberAttribute(WidgetType& Widget, const ObjectType& InitialValue, const TAttribute<ObjectType>& Attribute)
			: Super(Widget, InitialValue, Attribute)
		{
			VerifyAttributeAddress(Widget, this);
		}

		template<typename WidgetType, typename U = typename std::enable_if<std::is_base_of<SWidget, WidgetType>::value>::type>
		TSlateMemberAttribute(WidgetType& Widget, ObjectType&& InitialValue, const FGetter& Getter)
			: Super(Widget, MoveTemp(InitialValue), Getter)
		{
			VerifyAttributeAddress(Widget, this);
		}

		template<typename WidgetType, typename U = typename std::enable_if<std::is_base_of<SWidget, WidgetType>::value>::type>
		TSlateMemberAttribute(WidgetType& Widget, ObjectType&& InitialValue, FGetter&& Getter)
			: Super(Widget, MoveTemp(InitialValue), MoveTemp(Getter))
		{
			VerifyAttributeAddress(Widget, this);
		}

		template<typename WidgetType, typename U = typename std::enable_if<std::is_base_of<SWidget, WidgetType>::value>::type>
		TSlateMemberAttribute(WidgetType& Widget, ObjectType&& InitialValue, const TAttribute<ObjectType>& Attribute)
			: Super(Widget, MoveTemp(InitialValue), Attribute)
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

		TSlateManagedAttribute(TSharedRef<SWidget> Widget)
			: Super(Widget.Get())
			, ManagedWidget(Widget)
		{ }

		TSlateManagedAttribute(TSharedRef<SWidget> Widget, const ObjectType& InValue)
			: Super(Widget.Get(), InValue)
			, ManagedWidget(Widget)
		{
		}

		TSlateManagedAttribute(TSharedRef<SWidget> Widget, ObjectType&& InValue)
			: Super(Widget.Get(), MoveTemp(InValue))
			, ManagedWidget(Widget)
		{
		}

		TSlateManagedAttribute(TSharedRef<SWidget> Widget, const ObjectType& InitialValue, const FGetter& Getter)
			: Super(Widget.Get(), InitialValue, Getter)
			, ManagedWidget(Widget)
		{
		}

		TSlateManagedAttribute(TSharedRef<SWidget> Widget, const ObjectType& InitialValue, FGetter&& Getter)
			: Super(Widget.Get(), InitialValue, MoveTemp(Getter))
			, ManagedWidget(Widget)
		{
		}

		TSlateManagedAttribute(TSharedRef<SWidget> Widget, const ObjectType& InitialValue, const TAttribute<ObjectType>& Attribute)
			: Super(Widget.Get(), InitialValue, Attribute)
			, ManagedWidget(Widget)
		{
		}

		TSlateManagedAttribute(TSharedRef<SWidget> Widget, ObjectType&& InitialValue, const FGetter& Getter)
			: Super(Widget.Get(), MoveTemp(InitialValue), Getter)
			, ManagedWidget(Widget)
		{
		}

		TSlateManagedAttribute(TSharedRef<SWidget> Widget, ObjectType&& InitialValue, FGetter&& Getter)
			: Super(Widget.Get(), MoveTemp(InitialValue), MoveTemp(Getter))
			, ManagedWidget(Widget)
		{
		}

		TSlateManagedAttribute(TSharedRef<SWidget> Widget, ObjectType&& InitialValue, const TAttribute<ObjectType>& Attribute)
			: Super(Widget.Get(), MoveTemp(InitialValue), Attribute)
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

		template<typename WidgetType, typename U = typename std::enable_if<std::is_base_of<SWidget, WidgetType>::value>::type>
		void Bind(typename FGetter::template TSPMethodDelegate_Const<WidgetType>::FMethodPtr MethodPtr)
		{
			if (TSharedPtr<SWidget> Pin = ManagedWidget.Pin())
			{
				Super::Bind(*Pin.Get(), FGetter::CreateSP(Pin, MethodPtr));
			}
		}

		void Assign(const TAttribute<ObjectType>& OtherAttribute)
		{
			if (TSharedPtr<SWidget> Pin = ManagedWidget.Pin())
			{
				Super::Assign(*Pin.Get(), OtherAttribute);
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

} // SlateAttributePrivate
