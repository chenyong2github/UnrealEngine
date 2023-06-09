// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Misc/AssertionMacros.h"
#include "Misc/OptionalFwd.h"
#include "Templates/UnrealTemplate.h"
#include "Serialization/Archive.h"

inline constexpr FNullOpt NullOpt{0};

namespace UE::Core::Private
{
	template <size_t N>
	struct TOptionalStorage
	{
		uint8 Storage[N];
		bool bIsSet;
	};
}

/**
 * When we have an optional value IsSet() returns true, and GetValue() is meaningful.
 * Otherwise GetValue() is not meaningful.
 */
template<typename OptionalType>
struct TOptional
{
public:
	using ElementType = OptionalType;
	
	/** Construct an OptionalType with a valid value. */
	TOptional(const OptionalType& InValue)
		: TOptional(InPlace, InValue)
	{
	}
	TOptional(OptionalType&& InValue)
		: TOptional(InPlace, MoveTempIfPossible(InValue))
	{
	}
	template <typename... ArgTypes>
	explicit TOptional(EInPlace, ArgTypes&&... Args)
	{
		// If this fails to compile when trying to call TOptional(EInPlace, ...) with a non-public constructor,
		// do not make TOptional a friend.
		//
		// Instead, prefer this pattern:
		//
		//     class FMyType
		//     {
		//     private:
		//         struct FPrivateToken { explicit FPrivateToken() = default; };
		//
		//     public:
		//         // This has an equivalent access level to a private constructor,
		//         // as only friends of FMyType will have access to FPrivateToken,
		//         // but the TOptional constructor can legally call it since it's public.
		//         explicit FMyType(FPrivateToken, int32 Int, float Real, const TCHAR* String);
		//     };
		//
		//     // Won't compile if the caller doesn't have access to FMyType::FPrivateToken
		//     TOptional<FMyType> Opt(InPlace, FMyType::FPrivateToken{}, 5, 3.14f, TEXT("Banana"));
		//
		new(&Value) OptionalType(Forward<ArgTypes>(Args)...);
		Value.bIsSet = true;
	}
	
	/** Construct an OptionalType with an invalid value. */
	TOptional(FNullOpt)
		: TOptional()
	{
	}

	/** Construct an OptionalType with no value; i.e. unset */
	TOptional()
	{
		Value.bIsSet = false;
	}

	~TOptional()
	{
		Reset();
	}

	/** Copy/Move construction */
	TOptional(const TOptional& Other)
	{
		bool bLocalIsSet = Other.Value.bIsSet;
		Value.bIsSet = bLocalIsSet;
		if (!bLocalIsSet)
		{
			return;
		}

		new(&Value) OptionalType(*(const OptionalType*)&Other.Value);
	}
	TOptional(TOptional&& Other)
	{
		bool bLocalIsSet = Other.Value.bIsSet;
		Value.bIsSet = bLocalIsSet;
		if (!bLocalIsSet)
		{
			return;
		}

		new(&Value) OptionalType(MoveTempIfPossible(*(OptionalType*)&Other.Value));
	}

	TOptional& operator=(const TOptional& Other)
	{
		if (&Other != this)
		{
			Reset();
			if (Other.Value.bIsSet)
			{
				new(&Value) OptionalType(*(const OptionalType*)&Other.Value);
				Value.bIsSet = true;
			}
		}
		return *this;
	}
	TOptional& operator=(TOptional&& Other)
	{
		if (&Other != this)
		{
			Reset();
			if (Other.Value.bIsSet)
			{
				new(&Value) OptionalType(MoveTempIfPossible(*(OptionalType*)&Other.Value));
				Value.bIsSet = true;
			}
		}
		return *this;
	}

	TOptional& operator=(const OptionalType& InValue)
	{
		if (&InValue != (const OptionalType*)&Value)
		{
			Emplace(InValue);
		}
		return *this;
	}
	TOptional& operator=(OptionalType&& InValue)
	{
		if (&InValue != (const OptionalType*)&Value)
		{
			Emplace(MoveTempIfPossible(InValue));
		}
		return *this;
	}

	void Reset()
	{
		if (Value.bIsSet)
		{
			Value.bIsSet = false;

			// We need a typedef here because VC won't compile the destructor call below if OptionalType itself has a member called OptionalType
			typedef OptionalType OptionalDestructOptionalType;
			((OptionalType*)&Value)->OptionalDestructOptionalType::~OptionalDestructOptionalType();
		}
	}

	template <typename... ArgsType>
	OptionalType& Emplace(ArgsType&&... Args)
	{
		Reset();

		// If this fails to compile when trying to call Emplace with a non-public constructor,
		// do not make TOptional a friend.
		//
		// Instead, prefer this pattern:
		//
		//     class FMyType
		//     {
		//     private:
		//         struct FPrivateToken { explicit FPrivateToken() = default; };
		//
		//     public:
		//         // This has an equivalent access level to a private constructor,
		//         // as only friends of FMyType will have access to FPrivateToken,
		//         // but Emplace can legally call it since it's public.
		//         explicit FMyType(FPrivateToken, int32 Int, float Real, const TCHAR* String);
		//     };
		//
		//     TOptional<FMyType> Opt:
		//
		//     // Won't compile if the caller doesn't have access to FMyType::FPrivateToken
		//     Opt.Emplace(FMyType::FPrivateToken{}, 5, 3.14f, TEXT("Banana"));
		//
		OptionalType* Result = new(&Value) OptionalType(Forward<ArgsType>(Args)...);
		Value.bIsSet = true;
		return *Result;
	}

	friend bool operator==(const TOptional& Lhs, const TOptional& Rhs)
	{
		bool bIsLhsSet = Lhs.Value.bIsSet;
		bool bIsRhsSet = Rhs.Value.bIsSet;
		if (bIsLhsSet != bIsRhsSet)
		{
			return false;
		}
		if (!bIsLhsSet) // both unset
		{
			return true;
		}
		return (*(const OptionalType*)&Lhs.Value) == (*(const OptionalType*)&Rhs.Value);
	}

	friend bool operator!=(const TOptional& Lhs, const TOptional& Rhs)
	{
		return !(Lhs == Rhs);
	}

	void Serialize(FArchive& Ar)
	{
		bool bOptionalIsSet = IsSet();
		if (Ar.IsLoading())
		{
			bool bOptionalWasSaved = false;
			Ar << bOptionalWasSaved;
			if (bOptionalWasSaved)
			{
				if (!bOptionalIsSet)
				{
					Emplace();
				}
				Ar << GetValue();
			}
			else
			{
				Reset();
			}
		}
		else
		{
			Ar << bOptionalIsSet;
			if (bOptionalIsSet)
			{
				Ar << GetValue();
			}
		}
	}

	/** @return true when the value is meaningful; false if calling GetValue() is undefined. */
	bool IsSet() const
	{
		return Value.bIsSet;
	}
	FORCEINLINE explicit operator bool() const
	{
		return IsSet();
	}

	/** @return The optional value; undefined when IsSet() returns false. */
	OptionalType& GetValue()
	{
		checkf(IsSet(), TEXT("It is an error to call GetValue() on an unset TOptional. Please either check IsSet() or use Get(DefaultValue) instead."));
		return *(OptionalType*)&Value;
	}
	FORCEINLINE const OptionalType& GetValue() const
	{
		return const_cast<TOptional*>(this)->GetValue();
	}

	OptionalType* operator->()
	{
		return &GetValue();
	}
	FORCEINLINE const OptionalType* operator->() const
	{
		return const_cast<TOptional*>(this)->operator->();
	}

	OptionalType& operator*()
	{
		return GetValue();
	}
	FORCEINLINE const OptionalType& operator*() const
	{
		return const_cast<TOptional*>(this)->operator*();
	}

	/** @return The optional value when set; DefaultValue otherwise. */
	const OptionalType& Get(const OptionalType& DefaultValue UE_LIFETIMEBOUND) const UE_LIFETIMEBOUND
	{
		return IsSet() ? *(const OptionalType*)&Value : DefaultValue;
	}

	/** @return A pointer to the optional value when set, nullptr otherwise. */
	OptionalType* GetPtrOrNull()
	{
		return IsSet() ? (OptionalType*)&Value : nullptr;
	}
	FORCEINLINE const OptionalType* GetPtrOrNull() const
	{
		return const_cast<TOptional*>(this)->GetPtrOrNull();
	}

private:
	using ValueStorageType = UE::Core::Private::TOptionalStorage<sizeof(OptionalType)>;
	alignas(OptionalType) ValueStorageType Value;
};

template<typename OptionalType>
FArchive& operator<<(FArchive& Ar, TOptional<OptionalType>& Optional)
{
	Optional.Serialize(Ar);
	return Ar;
}

/**
 * Trait which determines whether or not a type is a TOptional.
 */
template <typename T> static constexpr bool TIsTOptional_V                              = false;
template <typename T> static constexpr bool TIsTOptional_V<               TOptional<T>> = true;
template <typename T> static constexpr bool TIsTOptional_V<const          TOptional<T>> = true;
template <typename T> static constexpr bool TIsTOptional_V<      volatile TOptional<T>> = true;
template <typename T> static constexpr bool TIsTOptional_V<const volatile TOptional<T>> = true;
