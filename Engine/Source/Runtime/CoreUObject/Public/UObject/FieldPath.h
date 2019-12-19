// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SoftObjectPtr.h: Pointer to UObject asset, keeps extra information so that it is works even if the asset is not in memory
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Field.h"
#include "HAL/ThreadSafeCounter.h"

class UField;
class UObject;

struct COREUOBJECT_API FFieldPath
{
protected:
	/** Untracked pointer to the resolved property */
	mutable FField* ResolvedField;
	/** GC tracked index of property owner UObject */
	mutable int32 ResolvedFieldOwner;
	/** Serial number this FFieldPath was last resolved with */
	mutable int32 SerialNumber;

	/** Path to the FField object from the innermost FField to the outermost UObject (UPackage) */
	TArray<FName> Path;

	/** Global serial number that gets increased each time UStruct destyroys its properties */
	static int32 GlobalSerialNumber;

	FORCEINLINE bool NeedsResolving() const
	{
		return !ResolvedField || SerialNumber != GlobalSerialNumber;
	}

public:

	FFieldPath()
		: ResolvedField(nullptr)
		, ResolvedFieldOwner(-1)
		, SerialNumber(-1)
	{}

	FFieldPath(FField* InField)
		: ResolvedField(InField)
		, ResolvedFieldOwner(-1)
		, SerialNumber(-1)
	{
		Generate(InField);
	}

	FFieldPath(const FFieldPath& Other)
		: ResolvedField(Other.ResolvedField)
		, ResolvedFieldOwner(Other.ResolvedFieldOwner)
		, SerialNumber(Other.SerialNumber)
		, Path(Other.Path)
	{
	}

#if WITH_EDITORONLY_DATA
	FFieldPath(UField* InField, const FName& InPropertyTypeName);
#endif

	/** Generates path from the passed in field pointer */
	void Generate(FField* InField);

	/** Generates path from the passed in field pointer */
	void Generate(const TCHAR* InFieldPathString);

#if WITH_EDITORONLY_DATA
	void GenerateFromUField(UField* InField);
#endif

	/** Clears the cached value so that the next time Get() is called, it will be resolved again */
	inline void ClearCachedField()
	{
		ResolvedField = nullptr;
		ResolvedFieldOwner = -1;
	}

	/**
	 * Tries to resolve the path without caching the resolved pointer 
	 * @param InCurrentStruct Struct that's trying to resolve this field path
	 * @param OutOwnerIndex ObjectIndex of the Owner UObject
	 * @return Resolved field or null
	 */
	FField* TryToResolvePath(UStruct* InCurrentStruct = nullptr, int32* OutOwnerIndex = nullptr) const;

	/**
	 * Tries to resolve the path and caches the result
	 * @param ExpectedClass Expected class of the resolved field
	 * @param InCurrentStruct Struct that's trying to resolve this field path	 
	 */
	FORCEINLINE void ResolveField(FFieldClass* ExpectedClass = FField::StaticClass(), UStruct* InCurrentStruct = nullptr) const
	{
		int32 FoundOwner = -1;
		FField* FoundField = TryToResolvePath(InCurrentStruct, &FoundOwner);
		if (FoundField && FoundField->IsA(ExpectedClass))
		{
			ResolvedField = FoundField;
			ResolvedFieldOwner = FoundOwner;
			SerialNumber = GlobalSerialNumber;
		}
		else
		{
			ResolvedField = nullptr;
			ResolvedFieldOwner = -1;
		}
	}

	/** Returns true if the field path is empty */
	inline bool IsEmpty() const
	{
		return !Path.Num();
	}

	/**
	* Slightly different than !IsValid(), returns true if this used to point to a FField, but doesn't any more and has not been assigned or reset in the mean time.
	* @return true if this used to point at a real object but no longer does.
	**/
	inline bool IsStale() const
	{
		return ResolvedField && (TryToResolvePath() != ResolvedField);
	}

	/**
	* Reset the weak pointer back to the NULL state
	*/
	inline void Reset()
	{
		ClearCachedField();
		Path.Empty();
	}

	inline bool IsPathIdentical(const FFieldPath& InOther) const
	{
		return Path == InOther.Path;
	}

	FString ToString() const;

	friend FArchive& operator<<(FArchive& Ar, FFieldPath& InOutPropertyPath)
	{
		Ar << InOutPropertyPath.Path;
		if (Ar.IsLoading())
		{
			InOutPropertyPath.ClearCachedField();
		}
		return Ar;
	}

	/** FOR INTERNAL USE ONLY: gets the pointer to the resolved field without trying to resolve it */
	inline FField*& GetResolvedFieldInternal()
	{
		return ResolvedField;
	}
	inline int32& GetResolvedFieldOwnerInternal()
	{
		return ResolvedFieldOwner;
	}

	/** Called when fields have been deleted to bump the global serial number and invalidate cached pointers */
	static void OnFieldDeleted();
};

template<class PropertyType>
struct TFieldPath : public FFieldPath
{
private:

	// These exists only to disambiguate the two constructors below
	enum EDummy1 { Dummy1 };

public:
	TFieldPath()
	{}
	TFieldPath(const TFieldPath&) = default;
	TFieldPath& operator=(const TFieldPath&) = default;

	/**
	* Construct from a null pointer
	**/
	FORCEINLINE TFieldPath(TYPE_OF_NULLPTR)
		: FFieldPath()
	{
	}

	/**
	* Construct from a string
	**/
	FORCEINLINE TFieldPath(const TCHAR* InPath)
		: FFieldPath()
	{
		Generate(InPath);
	}

#if WITH_EDITORONLY_DATA
	TFieldPath(UField* InField)
		: FFieldPath(InField, PropertyType::StaticClass()->GetFName())
	{
	}
#endif

	/**
	* Construct from an object pointer
	* @param Object object to create a weak pointer to
	**/
	template <
		typename OtherPropertyType,
		typename = decltype(ImplicitConv<PropertyType*>((OtherPropertyType*)nullptr))
	>
	FORCEINLINE TFieldPath(OtherPropertyType* InProperty, EDummy1 = Dummy1)
		: FFieldPath((FField*)CastField<PropertyType>(InProperty))
	{
		// This static assert is in here rather than in the body of the class because we want
		// to be able to define TFieldPath<UUndefinedClass>.
		static_assert(TPointerIsConvertibleFromTo<PropertyType, const volatile FField>::Value, "TFieldPath can only be constructed with FField types");
	}

	/**
	* Construct from another weak pointer of another type, intended for derived-to-base conversions
	* @param Other weak pointer to copy from
	**/
	template <typename OtherPropertyType>
	FORCEINLINE TFieldPath(const TFieldPath<OtherPropertyType>& Other)
		: FFieldPath(Other)
	{
		// It's also possible that this static_assert may fail for valid conversions because
		// one or both of the types have only been forward-declared.
		static_assert(TPointerIsConvertibleFromTo<OtherPropertyType, PropertyType>::Value, "Unable to convert TFieldPath - types are incompatible");
	}

	/**
	* Copy from an object pointer
	* @param Object object to create a weak pointer to
	**/
	template<class OtherPropertyType>
	FORCEINLINE typename TEnableIf<!TLosesQualifiersFromTo<OtherPropertyType, PropertyType>::Value>::Type operator=(OtherPropertyType* InProperty)
	{
		ResolvedField = InProperty; 
		Generate(ResolvedField);
	}

	/**
	* Assign from another weak pointer, intended for derived-to-base conversions
	* @param Other weak pointer to copy from
	**/
	template <typename OtherPropertyType>
	FORCEINLINE void operator=(const TFieldPath<OtherPropertyType>& Other)
	{
		// It's also possible that this static_assert may fail for valid conversions because
		// one or both of the types have only been forward-declared.
		static_assert(TPointerIsConvertibleFromTo<OtherPropertyType, PropertyType>::Value, "Unable to convert TFieldPath - types are incompatible");

		ResolvedField = Other.ResolvedField;
		ResolvedFieldOwner = Other.ResolvedFieldOwner;
		SerialNumber = Other.SerialNumber;
		Path = Other.Path;
	}

	/**
	* Dereference the weak pointer
	* @param bEvenIfPendingKill, if this is true, pendingkill objects are considered valid
	* @return NULL if this object is gone or the weak pointer was NULL, otherwise a valid uobject pointer
	**/
	FORCEINLINE PropertyType* Get(UStruct* InCurrentStruct = nullptr) const
	{
		if (NeedsResolving() && Path.Num())
		{
			ResolveField(PropertyType::StaticClass(), InCurrentStruct);
		}
		return static_cast<PropertyType*>(ResolvedField);
	}

	/**
	* Dereference the weak pointer
	**/
	FORCEINLINE PropertyType* operator*() const
	{
		return Get();
	}

	/**
	* Dereference the weak pointer
	**/
	FORCEINLINE PropertyType* operator->() const
	{
		return Get();
	}

	/** Hash function. */
	FORCEINLINE friend uint32 GetTypeHash(const TFieldPath& InPropertyPath)
	{
		uint32 HashValue = 0;
		if (InPropertyPath.Path.Num())
		{
			HashValue = GetTypeHash(InPropertyPath.Path[0]);
			for (int32 PathIndex = 1; PathIndex < InPropertyPath.Path.Num(); ++PathIndex)
			{
				HashValue = HashCombine(HashValue, GetTypeHash(InPropertyPath.Path[PathIndex]));
			}
		}
		return HashValue;
	}

	/**
	* Compare weak pointers for equality
	* @param Other weak pointer to compare to
	**/
	template <typename OtherPropertyType>
	FORCEINLINE bool operator==(const TFieldPath<OtherPropertyType> &Other) const
	{
		static_assert(TPointerIsConvertibleFromTo<OtherPropertyType, FField>::Value, "TFieldPath can only be compared with FField types");
		static_assert(TPointerIsConvertibleFromTo<PropertyType, OtherPropertyType>::Value || TPointerIsConvertibleFromTo<PropertyType, OtherPropertyType>::Value, "Unable to compare TFieldPath with raw pointer - types are incompatible");

		return Path == Other.Path;
	}

	/**
	* Compare weak pointers for inequality
	* @param Other weak pointer to compare to
	**/
	template <typename OtherPropertyType>
	FORCEINLINE bool operator!=(const TFieldPath<OtherPropertyType> &Other) const
	{
		static_assert(TPointerIsConvertibleFromTo<OtherPropertyType, FField>::Value, "TFieldPath can only be compared with FField types");
		static_assert(TPointerIsConvertibleFromTo<PropertyType, OtherPropertyType>::Value || TPointerIsConvertibleFromTo<PropertyType, OtherPropertyType>::Value, "Unable to compare TFieldPath with raw pointer - types are incompatible");

		return Path != Other.Path;
	}

	/**
	* Compare weak pointers for equality
	* @param Other pointer to compare to
	**/
	template <typename OtherPropertyType>
	FORCEINLINE bool operator==(const OtherPropertyType* Other) const
	{
		static_assert(TPointerIsConvertibleFromTo<OtherPropertyType, FField>::Value, "TFieldPath can only be compared with FField types");
		static_assert(TPointerIsConvertibleFromTo<PropertyType, OtherPropertyType>::Value || TPointerIsConvertibleFromTo<PropertyType, OtherPropertyType>::Value, "Unable to compare TFieldPath with raw pointer - types are incompatible");

		return Get() == Other;
	}

	/**
	* Compare weak pointers for inequality
	* @param Other pointer to compare to
	**/
	template <typename OtherPropertyType>
	FORCEINLINE bool operator!=(const OtherPropertyType* Other) const
	{
		static_assert(TPointerIsConvertibleFromTo<OtherPropertyType, FField>::Value, "TFieldPath can only be compared with FField types");
		static_assert(TPointerIsConvertibleFromTo<PropertyType, OtherPropertyType>::Value || TPointerIsConvertibleFromTo<PropertyType, OtherPropertyType>::Value, "Unable to compare TFieldPath with raw pointer - types are incompatible");

		return Get() != Other;
	}
};

// Helper function which deduces the type of the initializer
template <typename PropertyType>
FORCEINLINE TFieldPath<PropertyType> MakePropertyPath(PropertyType* Ptr)
{
	return TFieldPath<PropertyType>(Ptr);
}

template <typename LhsT, typename RhsT>
FORCENOINLINE bool operator==(const LhsT* Lhs, const TFieldPath<RhsT>& Rhs)
{
	// It's also possible that these static_asserts may fail for valid conversions because
	// one or both of the types have only been forward-declared.
	static_assert(TPointerIsConvertibleFromTo<LhsT, FField>::Value, "TFieldPath can only be compared with FField types");
	static_assert(TPointerIsConvertibleFromTo<LhsT, RhsT>::Value || TPointerIsConvertibleFromTo<RhsT, LhsT>::Value, "Unable to compare TFieldPath with raw pointer - types are incompatible");

	return Rhs == Lhs;
}

template <typename LhsT>
FORCENOINLINE bool operator==(const TFieldPath<LhsT>& Lhs, TYPE_OF_NULLPTR)
{
	return !Lhs.Get();
}

template <typename RhsT>
FORCENOINLINE bool operator==(TYPE_OF_NULLPTR, const TFieldPath<RhsT>& Rhs)
{
	return !Rhs.Get();
}

template <typename LhsT, typename RhsT>
FORCENOINLINE bool operator!=(const LhsT* Lhs, const TFieldPath<RhsT>& Rhs)
{
	// It's also possible that these static_asserts may fail for valid conversions because
	// one or both of the types have only been forward-declared.
	static_assert(TPointerIsConvertibleFromTo<LhsT, FField>::Value, "TFieldPath can only be compared with FField types");
	static_assert(TPointerIsConvertibleFromTo<LhsT, RhsT>::Value || TPointerIsConvertibleFromTo<RhsT, LhsT>::Value, "Unable to compare TFieldPath with raw pointer - types are incompatible");

	return Rhs != Lhs;
}

template <typename LhsT>
FORCENOINLINE bool operator!=(const TFieldPath<LhsT>& Lhs, TYPE_OF_NULLPTR)
{
	return !!Lhs.Get();
}

template <typename RhsT>
FORCENOINLINE bool operator!=(TYPE_OF_NULLPTR, const TFieldPath<RhsT>& Rhs)
{
	return !!Rhs.Get();
}

template<class T> struct TIsPODType<TFieldPath<T> > { enum { Value = true }; };
template<class T> struct TIsZeroConstructType<TFieldPath<T> > { enum { Value = true }; };
template<class T> struct TIsWeakPointerType<TFieldPath<T> > { enum { Value = true }; };


/**
* MapKeyFuncs for TFieldPath which allow the key to become stale without invalidating the map.
*/
template <typename KeyType, typename ValueType, bool bInAllowDuplicateKeys = false>
struct TPropertyPathMapKeyFuncs : public TDefaultMapKeyFuncs<KeyType, ValueType, bInAllowDuplicateKeys>
{
	typedef typename TDefaultMapKeyFuncs<KeyType, ValueType, bInAllowDuplicateKeys>::KeyInitType KeyInitType;

	static FORCEINLINE bool Matches(KeyInitType A, KeyInitType B)
	{
		return A == B;
	}

	static FORCEINLINE uint32 GetKeyHash(KeyInitType Key)
	{
		return GetTypeHash(Key);
	}
};


