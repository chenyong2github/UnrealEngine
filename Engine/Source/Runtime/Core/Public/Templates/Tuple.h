// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Templates/AreTypesEqual.h"
#include "Templates/UnrealTemplate.h"
#include "Templates/Decay.h"
#include "Delegates/IntegerSequence.h"
#include "Templates/Invoke.h"
#include "Serialization/StructuredArchive.h"

class FArchive;

template <typename... Types>
struct TTuple;

namespace UE4Tuple_Private
{
	template <int32 N, typename... Types>
	struct TNthTypeFromParameterPack;

	template <int32 N, typename T, typename... OtherTypes>
	struct TNthTypeFromParameterPack<N, T, OtherTypes...>
	{
		typedef typename TNthTypeFromParameterPack<N - 1, OtherTypes...>::Type Type;
	};

	template <typename T, typename... OtherTypes>
	struct TNthTypeFromParameterPack<0, T, OtherTypes...>
	{
		typedef T Type;
	};

	template <typename T, typename... Types>
	struct TDecayedFrontOfParameterPackIsSameType
	{
		enum { Value = TAreTypesEqual<T, typename TDecay<typename TNthTypeFromParameterPack<0, Types...>::Type>::Type>::Value };
	};

	template <typename T, uint32 Index>
	struct TTupleElement
	{
		template <
			typename... ArgTypes,
			typename = typename TEnableIf<
				TAndValue<
					sizeof...(ArgTypes) != 0,
					TOrValue<
						sizeof...(ArgTypes) != 1,
						TNot<UE4Tuple_Private::TDecayedFrontOfParameterPackIsSameType<TTupleElement, ArgTypes...>>
					>
				>::Value
			>::Type
		>
		explicit TTupleElement(ArgTypes&&... Args)
			: Value(Forward<ArgTypes>(Args)...)
		{
		}

		TTupleElement()
			: Value()
		{
		}

		TTupleElement(TTupleElement&&) = default;
		TTupleElement(const TTupleElement&) = default;
		TTupleElement& operator=(TTupleElement&&) = default;
		TTupleElement& operator=(const TTupleElement&) = default;

		T Value;
	};

	template <uint32 IterIndex, uint32 Index, typename... Types>
	struct TTupleElementHelperImpl;

	template <uint32 IterIndex, uint32 Index, typename ElementType, typename... Types>
	struct TTupleElementHelperImpl<IterIndex, Index, ElementType, Types...> : TTupleElementHelperImpl<IterIndex + 1, Index, Types...>
	{
	};

	template <uint32 Index, typename ElementType, typename... Types>
	struct TTupleElementHelperImpl<Index, Index, ElementType, Types...>
	{
		typedef ElementType Type;

		template <typename TupleType>
		static FORCEINLINE ElementType& Get(TupleType& Tuple)
		{
			return static_cast<TTupleElement<ElementType, Index>&>(Tuple).Value;
		}

		template <typename TupleType>
		static FORCEINLINE const ElementType& Get(const TupleType& Tuple)
		{
			return Get((TupleType&)Tuple);
		}
	};

	template <uint32 WantedIndex, typename... Types>
	struct TTupleElementHelper : TTupleElementHelperImpl<0, WantedIndex, Types...>
	{
	};

	template <uint32 ArgCount, uint32 ArgToCompare>
	struct FEqualityHelper
	{
		template <typename TupleType>
		FORCEINLINE static bool Compare(const TupleType& Lhs, const TupleType& Rhs)
		{
			return Lhs.template Get<ArgToCompare>() == Rhs.template Get<ArgToCompare>() && FEqualityHelper<ArgCount, ArgToCompare + 1>::Compare(Lhs, Rhs);
		}
	};

	template <uint32 ArgCount>
	struct FEqualityHelper<ArgCount, ArgCount>
	{
		template <typename TupleType>
		FORCEINLINE static bool Compare(const TupleType& Lhs, const TupleType& Rhs)
		{
			return true;
		}
	};

	template <uint32 NumArgs, uint32 ArgToCompare = 0, bool Last = ArgToCompare + 1 == NumArgs>
	struct TLessThanHelper
	{
		template <typename TupleType>
		FORCEINLINE static bool Do(const TupleType& Lhs, const TupleType& Rhs)
		{
			return Lhs.template Get<ArgToCompare>() < Rhs.template Get<ArgToCompare>() || (!(Rhs.template Get<ArgToCompare>() < Lhs.template Get<ArgToCompare>()) && TLessThanHelper<NumArgs, ArgToCompare + 1>::Do(Lhs, Rhs));
		}
	};

	template <uint32 NumArgs, uint32 ArgToCompare>
	struct TLessThanHelper<NumArgs, ArgToCompare, true>
	{
		template <typename TupleType>
		FORCEINLINE static bool Do(const TupleType& Lhs, const TupleType& Rhs)
		{
			return Lhs.template Get<ArgToCompare>() < Rhs.template Get<ArgToCompare>();
		}
	};

	template <uint32 NumArgs>
	struct TLessThanHelper<NumArgs, NumArgs, false>
	{
		template <typename TupleType>
		FORCEINLINE static bool Do(const TupleType& Lhs, const TupleType& Rhs)
		{
			return false;
		}
	};

	template <typename Indices, typename... Types>
	struct TTupleStorage;

	template <uint32... Indices, typename... Types>
	struct TTupleStorage<TIntegerSequence<uint32, Indices...>, Types...> : TTupleElement<Types, Indices>...
	{
		template <
			typename... ArgTypes,
			typename = typename TEnableIf<
				TAndValue<
					sizeof...(ArgTypes) == sizeof...(Types) && sizeof...(ArgTypes) != 0,
					TOrValue<
						sizeof...(ArgTypes) != 1,
						TNot<UE4Tuple_Private::TDecayedFrontOfParameterPackIsSameType<TTupleStorage, ArgTypes...>>
					>
				>::Value
			>::Type
		>
		explicit TTupleStorage(ArgTypes&&... Args)
			: TTupleElement<Types, Indices>(Forward<ArgTypes>(Args))...
		{
		}

		TTupleStorage() = default;
		TTupleStorage(TTupleStorage&&) = default;
		TTupleStorage(const TTupleStorage&) = default;
		TTupleStorage& operator=(TTupleStorage&&) = default;
		TTupleStorage& operator=(const TTupleStorage&) = default;

		template <uint32 Index> FORCEINLINE const typename TTupleElementHelper<Index, Types...>::Type& Get() const { return TTupleElementHelper<Index, Types...>::Get(*this); }
		template <uint32 Index> FORCEINLINE       typename TTupleElementHelper<Index, Types...>::Type& Get()       { return TTupleElementHelper<Index, Types...>::Get(*this); }
	};

	// Specialization of 2-TTuple to give it the API of TPair.
	template <typename InKeyType, typename InValueType>
	struct TTupleStorage<TIntegerSequence<uint32, 0, 1>, InKeyType, InValueType>
	{
	private:
		template <uint32 Index, typename Dummy> // Dummy needed for partial template specialization workaround
		struct TGetHelper;

		template <typename Dummy>
		struct TGetHelper<0, Dummy>
		{
			typedef InKeyType ResultType;

			static const InKeyType& Get(const TTupleStorage& Tuple) { return Tuple.Key; }
			static       InKeyType& Get(      TTupleStorage& Tuple) { return Tuple.Key; }
		};

		template <typename Dummy>
		struct TGetHelper<1, Dummy>
		{
			typedef InValueType ResultType;

			static const InValueType& Get(const TTupleStorage& Tuple) { return Tuple.Value; }
			static       InValueType& Get(      TTupleStorage& Tuple) { return Tuple.Value; }
		};

	public:
		typedef InKeyType   KeyType;
		typedef InValueType ValueType;

		template <typename KeyInitType, typename ValueInitType>
		explicit TTupleStorage(KeyInitType&& KeyInit, ValueInitType&& ValueInit)
			: Key  (Forward<KeyInitType  >(KeyInit  ))
			, Value(Forward<ValueInitType>(ValueInit))
		{
		}

		TTupleStorage()
			: Key()
			, Value()
		{
		}

		TTupleStorage(TTupleStorage&&) = default;
		TTupleStorage(const TTupleStorage&) = default;
		TTupleStorage& operator=(TTupleStorage&&) = default;
		TTupleStorage& operator=(const TTupleStorage&) = default;

		template <uint32 Index> FORCEINLINE const typename TGetHelper<Index, void>::ResultType& Get() const { return TGetHelper<Index, void>::Get(*this); }
		template <uint32 Index> FORCEINLINE       typename TGetHelper<Index, void>::ResultType& Get()       { return TGetHelper<Index, void>::Get(*this); }

		InKeyType   Key;
		InValueType Value;
	};

	template <typename Indices, typename... Types>
	struct TTupleImpl;

	template <uint32... Indices, typename... Types>
	struct TTupleImpl<TIntegerSequence<uint32, Indices...>, Types...> : TTupleStorage<TIntegerSequence<uint32, Indices...>, Types...>
	{
	private:
		typedef TTupleStorage<TIntegerSequence<uint32, Indices...>, Types...> Super;

	public:
		using Super::Get;

		template <
			typename... ArgTypes,
			typename = typename TEnableIf<
				TAndValue<
					sizeof...(ArgTypes) == sizeof...(Types) && sizeof...(ArgTypes) != 0,
					TOrValue<
						sizeof...(ArgTypes) != 1,
						TNot<UE4Tuple_Private::TDecayedFrontOfParameterPackIsSameType<TTupleImpl, ArgTypes...>>
					>
				>::Value
			>::Type
		>
		explicit TTupleImpl(ArgTypes&&... Args)
			: Super(Forward<ArgTypes>(Args)...)
		{
		}

		TTupleImpl() = default;
		TTupleImpl(TTupleImpl&& Other) = default;
		TTupleImpl(const TTupleImpl& Other) = default;
		TTupleImpl& operator=(TTupleImpl&& Other) = default;
		TTupleImpl& operator=(const TTupleImpl& Other) = default;

		template <typename FuncType, typename... ArgTypes>
		#if PLATFORM_COMPILER_HAS_DECLTYPE_AUTO
			decltype(auto) ApplyAfter(FuncType&& Func, ArgTypes&&... Args) const
		#else
			auto ApplyAfter(FuncType&& Func, ArgTypes&&... Args) const -> decltype(Func(Forward<ArgTypes>(Args)..., this->Get<Indices>()...))
		#endif
		{
			return Func(Forward<ArgTypes>(Args)..., this->template Get<Indices>()...);
		}

		template <typename FuncType, typename... ArgTypes>
		#if PLATFORM_COMPILER_HAS_DECLTYPE_AUTO
			decltype(auto) ApplyBefore(FuncType&& Func, ArgTypes&&... Args) const
		#else
			auto ApplyBefore(FuncType&& Func, ArgTypes&&... Args) const -> decltype(Func(this->Get<Indices>()..., Forward<ArgTypes>(Args)...))
		#endif
		{
			return Func(this->template Get<Indices>()..., Forward<ArgTypes>(Args)...);
		}

		FORCEINLINE friend FArchive& operator<<(FArchive& Ar, TTupleImpl& Tuple)
		{
			// This should be implemented with a fold expression when our compilers support it
			int Temp[] = { 0, (Ar << Tuple.template Get<Indices>(), 0)... };
			(void)Temp;
			return Ar;
		}

		FORCEINLINE friend void operator<<(FStructuredArchive::FSlot Slot, TTupleImpl& Tuple)
		{
			// This should be implemented with a fold expression when our compilers support it
			FStructuredArchive::FStream Stream = Slot.EnterStream();
			int Temp[] = { 0, (Stream.EnterElement() << Tuple.template Get<Indices>(), 0)... };
			(void)Temp;
		}

		FORCEINLINE friend bool operator==(const TTupleImpl& Lhs, const TTupleImpl& Rhs)
		{
			// This could be implemented with a fold expression when our compilers support it
			return FEqualityHelper<sizeof...(Types), 0>::Compare(Lhs, Rhs);
		}

		FORCEINLINE friend bool operator!=(const TTupleImpl& Lhs, const TTupleImpl& Rhs)
		{
			return !(Lhs == Rhs);
		}

		FORCEINLINE friend bool operator<(const TTupleImpl& Lhs, const TTupleImpl& Rhs)
		{
			return TLessThanHelper<sizeof...(Types)>::Do(Lhs, Rhs);
		}

		FORCEINLINE friend bool operator<=(const TTupleImpl& Lhs, const TTupleImpl& Rhs)
		{
			return !(Rhs < Lhs);
		}

		FORCEINLINE friend bool operator>(const TTupleImpl& Lhs, const TTupleImpl& Rhs)
		{
			return Rhs < Lhs;
		}

		FORCEINLINE friend bool operator>=(const TTupleImpl& Lhs, const TTupleImpl& Rhs)
		{
			return !(Lhs < Rhs);
		}
	};


	template <typename... Types>
	FORCEINLINE TTuple<typename TDecay<Types>::Type...> MakeTupleImpl(Types&&... Args)
	{
		return TTuple<typename TDecay<Types>::Type...>(Forward<Types>(Args)...);
	}

	template <typename IntegerSequence>
	struct TTransformTuple_Impl;

	template <uint32... Indices>
	struct TTransformTuple_Impl<TIntegerSequence<uint32, Indices...>>
	{
		template <typename TupleType, typename FuncType>
		#if PLATFORM_COMPILER_HAS_DECLTYPE_AUTO
			static decltype(auto) Do(TupleType&& Tuple, FuncType Func)
		#else
			static auto Do(TupleType&& Tuple, FuncType Func) -> decltype(MakeTuple(Func(Forward<TupleType>(Tuple).template Get<Indices>())...))
		#endif
		{
			return MakeTupleImpl(Func(Forward<TupleType>(Tuple).template Get<Indices>())...);
		}
	};

	template <typename IntegerSequence>
	struct TVisitTupleElements_Impl;

	template <uint32... Indices>
	struct TVisitTupleElements_Impl<TIntegerSequence<uint32, Indices...>>
	{
		// We need a second function to do the invocation for a particular index, to avoid the pack expansion being
		// attempted on the indices and tuples simultaneously.
		template <uint32 Index, typename FuncType, typename... TupleTypes>
		FORCEINLINE static void InvokeFunc(FuncType&& Func, TupleTypes&&... Tuples)
		{
			Invoke(Forward<FuncType>(Func), Forward<TupleTypes>(Tuples).template Get<Index>()...);
		}

		template <typename FuncType, typename... TupleTypes>
		static void Do(FuncType&& Func, TupleTypes&&... Tuples)
		{
			// This should be implemented with a fold expression when our compilers support it
			int Temp[] = { 0, (InvokeFunc<Indices>(Forward<FuncType>(Func), Forward<TupleTypes>(Tuples)...), 0)... };
			(void)Temp;
		}
	};


	template <typename TupleType>
	struct TCVTupleArity;

	template <typename... Types>
	struct TCVTupleArity<const volatile TTuple<Types...>>
	{
		enum { Value = sizeof...(Types) };
	};
}

template <typename... Types>
struct TTuple : UE4Tuple_Private::TTupleImpl<TMakeIntegerSequence<uint32, sizeof...(Types)>, Types...>
{
private:
	typedef UE4Tuple_Private::TTupleImpl<TMakeIntegerSequence<uint32, sizeof...(Types)>, Types...> Super;

public:
	template <
		typename... ArgTypes,
		typename = typename TEnableIf<
			TAndValue<
				sizeof...(ArgTypes) == sizeof...(Types) && sizeof...(ArgTypes) != 0,
				TOrValue<
					sizeof...(ArgTypes) != 1,
					TNot<UE4Tuple_Private::TDecayedFrontOfParameterPackIsSameType<TTuple, ArgTypes...>>
				>
			>::Value
		>::Type
	>
	explicit TTuple(ArgTypes&&... Args)
		: Super(Forward<ArgTypes>(Args)...)
	{
		// This constructor is disabled for TTuple and zero parameters because VC is incorrectly instantiating it as a move/copy/default constructor.
	}

	TTuple() = default;
	TTuple(TTuple&&) = default;
	TTuple(const TTuple&) = default;
	TTuple& operator=(TTuple&&) = default;
	TTuple& operator=(const TTuple&) = default;
};


/**
 * Traits class which calculates the number of elements in a tuple.
 */
template <typename TupleType>
struct TTupleArity : UE4Tuple_Private::TCVTupleArity<const volatile TupleType>
{
};


/**
 * Makes a TTuple from some arguments.  The type of the TTuple elements are the decayed versions of the arguments.
 *
 * @param  Args  The arguments used to construct the tuple.
 * @return A tuple containing a copy of the arguments.
 *
 * Example:
 *
 * void Func(const int32 A, FString&& B)
 * {
 *     // Equivalent to:
 *     // TTuple<int32, const TCHAR*, FString> MyTuple(A, TEXT("Hello"), MoveTemp(B));
 *     auto MyTuple = MakeTuple(A, TEXT("Hello"), MoveTemp(B));
 * }
 */
template <typename... Types>
FORCEINLINE TTuple<typename TDecay<Types>::Type...> MakeTuple(Types&&... Args)
{
	return UE4Tuple_Private::MakeTupleImpl(Forward<Types>(Args)...);
}


/**
 * Creates a new TTuple by applying a functor to each of the elements.
 *
 * @param  Tuple  The tuple to apply the functor to.
 * @param  Func   The functor to apply.
 *
 * @return A new tuple of the transformed elements.
 *
 * Example:
 *
 * float        Overloaded(int32 Arg);
 * char         Overloaded(const TCHAR* Arg);
 * const TCHAR* Overloaded(const FString& Arg);
 *
 * void Func(const TTuple<int32, const TCHAR*, FString>& MyTuple)
 * {
 *     // Equivalent to:
 *     // TTuple<float, char, const TCHAR*> TransformedTuple(Overloaded(MyTuple.Get<0>()), Overloaded(MyTuple.Get<1>()), Overloaded(MyTuple.Get<2>())));
 *     auto TransformedTuple = TransformTuple(MyTuple, [](const auto& Arg) { return Overloaded(Arg); });
 * }
 */
template <typename FuncType, typename... Types>
#if PLATFORM_COMPILER_HAS_DECLTYPE_AUTO
	FORCEINLINE decltype(auto) TransformTuple(TTuple<Types...>&& Tuple, FuncType Func)
#else
	FORCEINLINE auto TransformTuple(TTuple<Types...>&& Tuple, FuncType Func) -> decltype(UE4Tuple_Private::TTransformTuple_Impl<TMakeIntegerSequence<uint32, sizeof...(Types)>>::Do(MoveTemp(Tuple), MoveTemp(Func)))
#endif
{
	return UE4Tuple_Private::TTransformTuple_Impl<TMakeIntegerSequence<uint32, sizeof...(Types)>>::Do(MoveTemp(Tuple), MoveTemp(Func));
}

template <typename FuncType, typename... Types>
#if PLATFORM_COMPILER_HAS_DECLTYPE_AUTO
	FORCEINLINE decltype(auto) TransformTuple(const TTuple<Types...>& Tuple, FuncType Func)
#else
	FORCEINLINE auto TransformTuple(const TTuple<Types...>& Tuple, FuncType Func) -> decltype(UE4Tuple_Private::TTransformTuple_Impl<TMakeIntegerSequence<uint32, sizeof...(Types)>>::Do(Tuple, MoveTemp(Func)))
#endif
{
	return UE4Tuple_Private::TTransformTuple_Impl<TMakeIntegerSequence<uint32, sizeof...(Types)>>::Do(Tuple, MoveTemp(Func));
}


/**
 * Visits each element in the specified tuples in parallel and applies them as arguments to the functor.
 * All specified tuples must have the same number of elements.
 *
 * @param  Func    The functor to apply.
 * @param  Tuples  The tuples whose elements are to be applied to the functor.
 *
 * Example:
 *
 * void Func(const TTuple<int32, const TCHAR*, FString>& Tuple1, const TTuple<bool, float, FName>& Tuple2)
 * {
 *     // Equivalent to:
 *     // Functor(Tuple1.Get<0>(), Tuple2.Get<0>());
 *     // Functor(Tuple1.Get<1>(), Tuple2.Get<1>());
 *     // Functor(Tuple1.Get<2>(), Tuple2.Get<2>());
 *     VisitTupleElements(Functor, Tuple1, Tuple2);
 * }
 */
template <typename FuncType, typename FirstTupleType, typename... TupleTypes>
FORCEINLINE void VisitTupleElements(FuncType&& Func, FirstTupleType&& FirstTuple, TupleTypes&&... Tuples)
{
	UE4Tuple_Private::TVisitTupleElements_Impl<TMakeIntegerSequence<uint32, TTupleArity<typename TDecay<FirstTupleType>::Type>::Value>>::Do(Forward<FuncType>(Func), Forward<FirstTupleType>(FirstTuple), Forward<TupleTypes>(Tuples)...);
}
