// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Class.h"
#include "UObject/Field.h"
#include "Templates/ChooseClass.h"

#include <type_traits>

template <typename T>
class TSubclassOf;

template <typename T>
struct TIsTSubclassOf
{
	enum { Value = false };
};

template <typename T> struct TIsTSubclassOf<               TSubclassOf<T>> { enum { Value = true }; };
template <typename T> struct TIsTSubclassOf<const          TSubclassOf<T>> { enum { Value = true }; };
template <typename T> struct TIsTSubclassOf<      volatile TSubclassOf<T>> { enum { Value = true }; };
template <typename T> struct TIsTSubclassOf<const volatile TSubclassOf<T>> { enum { Value = true }; };

/**
 * Template to allow TClassTypes to be passed around with type safety 
 */
template <typename T>
class TSubclassOf
{
public:
	typedef typename TChooseClass<TIsDerivedFrom<T, FField>::IsDerived, FFieldClass, UClass>::Result TClassType;
	typedef typename TChooseClass<TIsDerivedFrom<T, FField>::IsDerived, FField, UObject>::Result TBaseType;

private:
	template <typename U>
	friend class TSubclassOf;

public:
	TSubclassOf() = default;
	TSubclassOf(TSubclassOf&&) = default;
	TSubclassOf(const TSubclassOf&) = default;
	TSubclassOf& operator=(TSubclassOf&&) = default;
	TSubclassOf& operator=(const TSubclassOf&) = default;
	~TSubclassOf() = default;

	/** Constructor that takes a UClass and does a runtime check to make sure this is a compatible class */
	FORCEINLINE TSubclassOf(TClassType* From)
		: Class(From)
	{
	}

	/**
	 * Construct from a UClass* or FFieldClass* (or something implicitly convertible to those)
	 * if T is a UObject or a FField type respectively.
	 */
	template <
		typename U,
		std::enable_if_t<
			!TIsTSubclassOf<std::decay_t<U>>::Value,
			decltype(ImplicitConv<TClassType*>(std::declval<U>()))
		>* = nullptr
	>
	FORCEINLINE TSubclassOf(U&& From)
		: Class(From)
	{
	}

	/** Construct from another TSubclassOf, only if types are compatible */
	template <
		typename OtherT,
		decltype(ImplicitConv<T*>((OtherT*)nullptr))* = nullptr
	>
	FORCEINLINE TSubclassOf(const TSubclassOf<OtherT>& From)
		: Class(*From)
	{
	}

	/** Assign from another TSubclassOf, only if types are compatible */
	template <
		typename OtherT,
		decltype(ImplicitConv<T*>((OtherT*)nullptr))* = nullptr
	>
	FORCEINLINE TSubclassOf& operator=(const TSubclassOf<OtherT>& From)
	{
		Class = *From;
		return *this;
	}

	/** Assign from a UClass* or FFieldClass*. */
	FORCEINLINE TSubclassOf& operator=(TClassType* From)
	{
		Class = From;
		return *this;
	}

	/**
	 * Assign from a UClass* or FFieldClass* (or something implicitly convertible to those).
	 */
	template <
		typename U,
		std::enable_if_t<
			!TIsTSubclassOf<std::decay_t<U>>::Value,
			decltype(ImplicitConv<TClassType*>(std::declval<U>()))
		>* = nullptr
	>
	FORCEINLINE TSubclassOf& operator=(U&& From)
	{
		Class = From;
		return *this;
	}

	/** Dereference back into a UClass* or FFieldClass*, does runtime type checking. */
	FORCEINLINE TClassType* operator*() const
	{
		if (!Class || !Class->IsChildOf(T::StaticClass()))
		{
			return nullptr;
		}
		return Class;
	}
	
	/** Dereference back into a UClass* or FFieldClass*, does runtime type checking. */
	FORCEINLINE TClassType* Get() const
	{
		return **this;
	}

	/** Dereference back into a UClass* or FFieldClass*, does runtime type checking. */
	FORCEINLINE TClassType* operator->() const
	{
		return **this;
	}

	/** Implicit conversion to UClass* or FFieldClass*, does runtime type checking. */
	FORCEINLINE operator TClassType*() const
	{
		return **this;
	}

	/**
	 * Get the CDO if we are referencing a valid class
	 *
	 * @return the CDO, or null if class is null
	 */
	FORCEINLINE T* GetDefaultObject() const
	{
		TBaseType* Result = nullptr;
		if (Class)
		{
			Result = Class->GetDefaultObject();
			check(Result && Result->IsA(T::StaticClass()));
		}
		return (T*)Result;
	}

	FORCEINLINE void SerializeTSubclassOf(FArchive& Ar)
	{
		Ar << Class;
	}

	friend uint32 GetTypeHash(const TSubclassOf& SubclassOf)
	{
		return GetTypeHash(SubclassOf.Class);
	}

#if DO_CHECK
	// This is a DEVELOPMENT ONLY debugging function and should not be relied upon. Client
	// systems should never require unsafe access to the referenced UClass
	UClass* DebugAccessRawClassPtr() const
	{
		return Class;
	}
#endif

private:
	TClassType* Class = nullptr;
};

template <typename T>
FArchive& operator<<(FArchive& Ar, TSubclassOf<T>& SubclassOf)
{
	SubclassOf.SerializeTSubclassOf(Ar);
	return Ar;
}

