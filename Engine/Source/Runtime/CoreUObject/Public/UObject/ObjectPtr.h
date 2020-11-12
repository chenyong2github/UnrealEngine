// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "UObject/Class.h"
#include "UObject/Object.h"
#include "UObject/ObjectHandle.h"

#include <type_traits>

#define UE_WITH_OBJECT_PTR_DEPRECATIONS 0
#if UE_WITH_OBJECT_PTR_DEPRECATIONS
	#define UE_OBJPTR_DEPRECATED(Version, Message) UE_DEPRECATED(Version, Message)
#else
	#define UE_OBJPTR_DEPRECATED(Version, Message) 
#endif

template <typename T>
struct TObjectPtr;

struct FObjectPtr
{
public:
	FObjectPtr() = default;

	FORCEINLINE FObjectPtr(TYPE_OF_NULLPTR)
		: Handle(MakeObjectHandle(nullptr))
	{
	}

	explicit FORCEINLINE FObjectPtr(UObject* Object)
		: Handle(MakeObjectHandle(Object))
	{
	}

	UE_OBJPTR_DEPRECATED(5.0, "Construction with incomplete type pointer is deprecated.  Please update this code to use MakeObjectPtrUnsafe.")
	explicit FORCEINLINE FObjectPtr(void* IncompleteObject)
		: Handle(MakeObjectHandle(reinterpret_cast<UObject*>(IncompleteObject)))
	{
	}

	explicit FORCEINLINE FObjectPtr(const FObjectRef& ObjectRef)
		: Handle(MakeObjectHandle(ObjectRef))
	{
	}

	explicit FORCEINLINE FObjectPtr(const FPackedObjectRef& PackedObjectRef)
		: Handle(MakeObjectHandle(PackedObjectRef))
	{
	}

	FORCEINLINE UObject* Get() const
	{
		return ResolveObjectHandle(Handle);
	}

	FORCEINLINE UClass* GetClass() const
	{
		return ResolveObjectHandleClass(Handle);
	}

	// @TODO: OBJPTR: Garbage collector interaction (for Rob's requested feature to make incremental
	//		 reachability analysis possible) could be hard coded here and not conditionally
	//		 compiled out.
	FObjectPtr(FObjectPtr&&) = default;
	FObjectPtr(const FObjectPtr&) = default;
	FObjectPtr& operator=(FObjectPtr&&) = default;
	FObjectPtr& operator=(const FObjectPtr&) = default;

	FObjectPtr& operator=(UObject* Other)
	{
		// @TODO: OBJPTR: Garbage collector interaction (for Rob's requested feature to make incremental
		//		 reachability analysis possible) could be hard coded here and not conditionally
		//		 compiled out.
		Handle = MakeObjectHandle(Other);
		return *this;
	}

	UE_OBJPTR_DEPRECATED(5.0, "Assignment with incomplete type pointer is deprecated.  Please update this code to use MakeObjectPtrUnsafe.")
	FObjectPtr& operator=(void* IncompleteOther)
	{
		// @TODO: OBJPTR: Garbage collector interaction (for Rob's requested feature to make incremental
		//		 reachability analysis possible) could be hard coded here and not conditionally
		//		 compiled out.
		Handle = MakeObjectHandle(reinterpret_cast<UObject*>(IncompleteOther));
		return *this;
	}

	FObjectPtr& operator=(TYPE_OF_NULLPTR)
	{
		// @TODO: OBJPTR: Garbage collector interaction (for Rob's requested feature to make incremental
		//		 reachability analysis possible) could be hard coded here and not conditionally
		//		 compiled out.
		Handle = MakeObjectHandle(nullptr);
		return *this;
	}

	FORCEINLINE bool operator==(FObjectPtr Other) const { return (Handle == Other.Handle); }
	FORCEINLINE bool operator!=(FObjectPtr Other) const { return (Handle != Other.Handle); }

	// @TODO: OBJPTR: ToTObjectPtr will be removed in the future when a proper casting layer is added
	UE_OBJPTR_DEPRECATED(5.0, "Use of ToTObjectPtr is unsafe and is deprecated.")
	FORCEINLINE TObjectPtr<UObject>& ToTObjectPtr();
	UE_OBJPTR_DEPRECATED(5.0, "Use of ToTObjectPtr is unsafe and is deprecated.")
	FORCEINLINE const TObjectPtr<UObject>& ToTObjectPtr() const;

	// @TODO: OBJPTR: The presence of this conversion operator triggers a bug in MSVC where implicit conversion of the
	//			derived TObjectPtr type to UObject* results in a compile error saying that the conversion  operator is
	//			in a private base class.  That shouldn't matter as the derived TObjectPtr type has its own templated
	//			conversion operator.  This issue does not come up in clang compiled builds.
	//FORCEINLINE operator UObject*() const { return Get();  }
	FORCEINLINE UObject* operator->() const { return Get(); }
	FORCEINLINE UObject& operator*() const { return *Get(); }

	FORCEINLINE bool IsNull() const { return IsObjectHandleNull(Handle); }
	explicit FORCEINLINE operator bool() const { return !IsNull(); }
	FORCEINLINE FObjectHandle GetHandle() const { return Handle; }
	FORCEINLINE FObjectHandle& GetHandleRef() const { return Handle; }

	FORCEINLINE bool IsA(const UClass* SomeBase) const
	{
		checkfSlow(SomeBase, TEXT("IsA(NULL) cannot yield meaningful results"));

		const UClass* ThisClass = GetClass();
		checkfSlow(ThisClass, TEXT("Object pointer class did not resolve to a non-null value"));

		return ThisClass->IsChildOf(SomeBase);
	}

	template <typename T>
	FORCEINLINE bool IsA() const
	{
		return IsA(T::StaticClass());
	}

private:
	friend FORCEINLINE uint32 GetTypeHash(FObjectPtr Object)
	{
		return GetTypeHash(Object.Handle);
	}

	mutable FObjectHandle Handle;
};

template <typename T>
struct TPrivateObjectPtr;

template <typename T>
struct TIsTObjectPtr
{
	enum { Value = false };
};

template <typename T>
struct TObjectPtr : private FObjectPtr
{
public:

	TObjectPtr() = default;
	TObjectPtr(TObjectPtr<T>&&) = default;
	TObjectPtr(const TObjectPtr<T>&) = default;

	FORCEINLINE TObjectPtr(TYPE_OF_NULLPTR)
		: FObjectPtr(nullptr)
	{
	}

	template <
		typename U,
		decltype(ImplicitConv<T*>(std::declval<U*>()))* = nullptr
	>
	FORCEINLINE TObjectPtr(const TObjectPtr<U>& Other)
		: FObjectPtr(static_cast<const FObjectPtr&>(Other))
	{
	}

	template <
		typename U,
		std::enable_if_t<
			!TIsTObjectPtr<std::decay_t<U>>::Value,
			decltype(ImplicitConv<T*>(std::declval<U>()))
		>* = nullptr
	>
	FORCEINLINE TObjectPtr(U&& Object)
		: FObjectPtr(const_cast<std::remove_const_t<T>*>(ImplicitConv<T*>(Object)))
	{
		// It would be desirable for this constructor to be implicit to allow:
		// 1) Uniform initialization syntax of a TObjectPtr field from a raw object pointer value
		// 2) Allowing the implicit construction of a TObjectPtr<Derived> from a raw object pointer to Base (or something implicitly convertible to that)
		// However making this implicit introduces a compile-time ambiguity in ternary operators of the form "condition ? TObjectPtr : raw pointer"
	}

	explicit FORCEINLINE TObjectPtr(TPrivateObjectPtr<T>&& PrivatePtr)
		: FObjectPtr(const_cast<UObject*>(PrivatePtr.Pointer))
	{
	}

	TObjectPtr<T>& operator=(TObjectPtr<T>&&) = default;
	TObjectPtr<T>& operator=(const TObjectPtr<T>&) = default;

	FORCEINLINE TObjectPtr<T>& operator=(TYPE_OF_NULLPTR)
	{
		FObjectPtr::operator=(nullptr);
		return *this;
	}

	template <
		typename U,
		decltype(ImplicitConv<T*>(std::declval<U*>()))* = nullptr
	>
	FORCEINLINE TObjectPtr<T>& operator=(const TObjectPtr<U>& Other)
	{
		FObjectPtr::operator=(static_cast<const FObjectPtr&>(Other));
		return *this;
	}

	template <
		typename U,
		std::enable_if_t<
			!TIsTObjectPtr<std::decay_t<U>>::Value,
			decltype(ImplicitConv<T*>(std::declval<U>()))
		>* = nullptr
	>
	FORCEINLINE TObjectPtr<T>& operator=(U&& Object)
	{
		FObjectPtr::operator=(const_cast<std::remove_const_t<T>*>(ImplicitConv<T*>(Object)));
		return *this;
	}

	FORCEINLINE TObjectPtr<T>& operator=(TPrivateObjectPtr<T>&& PrivatePtr)
	{
		FObjectPtr::operator=(const_cast<UObject*>(PrivatePtr.Pointer));
		return *this;
	}

	template <
		typename U,
		typename Base = std::decay_t<decltype(false ? std::declval<std::decay_t<T*>>() : std::declval<std::decay_t<U*>>())>
	>
	FORCEINLINE bool operator==(const TObjectPtr<U>& Other) const
	{
		return FObjectPtr::operator==(static_cast<const FObjectPtr&>(Other));
	}

	template <
		typename U,
		typename Base = std::decay_t<decltype(false ? std::declval<std::decay_t<T*>>() : std::declval<std::decay_t<U*>>())>
	>
	FORCEINLINE bool operator!=(const TObjectPtr<U>& Other) const
	{
		return FObjectPtr::operator!=(static_cast<const FObjectPtr&>(Other));
	}

	// @TODO: OBJPTR: There is a risk that the FObjectPtr is storing a reference to the wrong type.  This could
	//			happen if data was serialized at a time when a pointer field was declared to be of type A, but then the declaration
	//			changed and the pointer field is now of type B.  Upon deserialization of pre-existing data, we'll be holding
	//			a reference to the wrong type of object which we'll just send back static_casted as the wrong type.  Doing
	//			a check or checkSlow here could catch this, but it would be better if the check could happen elsewhere that
	//			isn't called as frequently.
	FORCEINLINE T* Get() const { return (T*)(FObjectPtr::Get()); }
	using FObjectPtr::GetClass;
	
	FORCEINLINE operator T* () const { return Get(); }
	template <typename U>
	UE_OBJPTR_DEPRECATED(5.0, "Explicit cast to other raw pointer types is deprecated.  Please use the Cast API or get the raw pointer with ToRawPtr and cast that instead.")
	explicit FORCEINLINE operator U* () const { return (U*)Get(); }
	explicit FORCEINLINE operator UPTRINT() const { return (UPTRINT)Get(); }
	FORCEINLINE T* operator->() const { return Get(); }
	FORCEINLINE T& operator*() const { return *Get(); }

	UE_OBJPTR_DEPRECATED(5.0, "Conversion to a mutable pointer is deprecated.  Please pass a TObjectPtr<T>& instead so that assignment can be tracked accurately.")
	FORCEINLINE operator T*& () { return GetInternalRef(); }

	using FObjectPtr::IsNull;
	using FObjectPtr::operator bool;
	using FObjectPtr::GetHandle;

	using FObjectPtr::IsA;

	friend FORCEINLINE uint32 GetTypeHash(const TObjectPtr<T>& ObjectPtr)
	{
		return GetTypeHash(static_cast<const FObjectPtr&>(ObjectPtr));
	}

	friend FORCEINLINE FArchive& operator<<(FArchive& Ar, TObjectPtr<T>& ObjectPtr)
	{
		Ar << static_cast<FObjectPtr&>(ObjectPtr);
		return Ar;
	}

	friend struct FObjectPtr;
	template <typename U> friend struct TObjectPtr;
private:
	// @TODO: OBJPTR: There is a risk of a gap in access tracking here.  The caller may get a mutable pointer, write to it, then
	//			read from it.  That last read would happen without an access being recorded.  Not sure if there is a good way
	//			to handle this case without forcing the calling code to be modified.
	FORCEINLINE T*& GetInternalRef()
	{
		FObjectPtr::Get();
		return (T*&)FObjectPtr::GetHandleRef();
	}
};

template <typename T>
struct TIsTObjectPtr<TObjectPtr<T>>
{
	enum { Value = true };
};

template <typename T>
struct TRemoveObjectPointer
{
	typedef T Type;
};
template <typename T>
struct TRemoveObjectPointer<TObjectPtr<T>>
{
	typedef T Type;
};

namespace ObjectPtr_Private
{
	/** Coerce to pointer through implicit conversion to const T* (overload through less specific "const T*" parameter to avoid ambiguity with other coercion options that may also exist. */
	template <typename T>
	FORCEINLINE const T* CoerceToPointer(const T* Other)
	{
		return Other;
	}

	/** Coerce to pointer through implicit conversion to CommonPointerType where CommonPointerType is deduced, and must be a C++ pointer, not a wrapper type. */
	template <
		typename T,
		typename U,
		typename CommonPointerType =  decltype(std::declval<bool>() ? std::declval<const T*>() : std::declval<U>()),
		std::enable_if_t<
			std::is_pointer<CommonPointerType>::value
		>* = nullptr
	>
	FORCEINLINE auto CoerceToPointer(U&& Other) -> CommonPointerType
	{
		return Other;
	}

	/** Coerce to pointer through the use of a ".Get()" member, which is the convention within Unreal smart pointer types. */
	template <
		typename T,
		typename U,
		std::enable_if_t<
			!TIsTObjectPtr<std::decay_t<U>>::Value,
			decltype(std::declval<U>().Get())
		>* = nullptr
	>
	FORCEINLINE auto CoerceToPointer(U&& Other) -> decltype(std::declval<U>().Get())
	{
		return Other.Get();
	}

	/** Perform shallow equality check between a TObjectPtr and another (non TObjectPtr) type that we can coerce to a pointer. */
	template <
		typename T,
		typename U,
		std::enable_if_t<
			!TIsTObjectPtr<std::decay_t<U>>::Value,
			decltype(CoerceToPointer<T>(std::declval<U>()) == std::declval<const T*>())
		>* = nullptr
	>
	FORCEINLINE bool IsObjectPtrEqual(const TObjectPtr<T>& Ptr, U&& Other)
	{
		auto OtherPtr = ObjectPtr_Private::CoerceToPointer<T>(Other);
		return (Ptr.IsNull() == (OtherPtr == nullptr)) && (Ptr.Get() == OtherPtr);
	}
}

// Equality/Inequality comparisons against another type that can be implicitly converted to the pointer type kept in a TObjectPtr
template <
	typename T,
	typename U,
	decltype(ObjectPtr_Private::IsObjectPtrEqual(std::declval<const TObjectPtr<T>&>(), std::declval<U&&>()))* = nullptr
>
FORCEINLINE bool operator==(const TObjectPtr<T>& Ptr, U&& Other)
{
	return ObjectPtr_Private::IsObjectPtrEqual(Ptr, Other);
}
template <
	typename T,
	typename U,
	decltype(ObjectPtr_Private::IsObjectPtrEqual(std::declval<const TObjectPtr<T>&>(), std::declval<U&&>()))* = nullptr
>
FORCEINLINE bool operator==(U&& Other, const TObjectPtr<T>& Ptr)
{
	return ObjectPtr_Private::IsObjectPtrEqual(Ptr, Other);
}
template <
	typename T,
	typename U,
	decltype(ObjectPtr_Private::IsObjectPtrEqual(std::declval<const TObjectPtr<T>&>(), std::declval<U&&>()))* = nullptr
>
FORCEINLINE bool operator!=(const TObjectPtr<T>& Ptr, U&& Other)
{
	return !ObjectPtr_Private::IsObjectPtrEqual(Ptr, Other);
}
template <
	typename T,
	typename U,
	decltype(ObjectPtr_Private::IsObjectPtrEqual(std::declval<const TObjectPtr<T>&>(), std::declval<U&&>()))* = nullptr
>
FORCEINLINE bool operator!=(U&& Other, const TObjectPtr<T>& Ptr)
{
	return !ObjectPtr_Private::IsObjectPtrEqual(Ptr, Other);
}

template <typename T>
TPrivateObjectPtr<T> MakeObjectPtrUnsafe(const UObject* Obj);

template <typename T>
struct TPrivateObjectPtr
{
public:
	TPrivateObjectPtr(const TPrivateObjectPtr<T>& Other) = default;

private:
	/** Only for use by MakeObjectPtrUnsafe */
	explicit TPrivateObjectPtr(const UObject* InPointer)
		: Pointer(InPointer)
	{
	}

	const UObject* Pointer;
	friend struct TObjectPtr<T>;
	friend TPrivateObjectPtr MakeObjectPtrUnsafe<T>(const UObject* Obj);
};

/** Used to allow the caller to provide a pointer to an incomplete type of T that has explicitly cast to a UObject. */
template <typename T>
TPrivateObjectPtr<T> MakeObjectPtrUnsafe(const UObject* Obj)
{
	return TPrivateObjectPtr<T>{Obj};
}

template <typename T>
FORCEINLINE T* ToRawPtr(const TObjectPtr<T>& Ptr)
{
	// NOTE: This is specifically not getting a reference to the internal pointer.
	return Ptr.Get();
}

template <typename T>
FORCEINLINE T* ToRawPtr(T* Ptr)
{
	return Ptr;
}

template <typename T, SIZE_T Size>
FORCEINLINE T** ToRawPtrArrayUnsafe(TObjectPtr<T>(&ArrayOfPtr)[Size])
{
	for (TObjectPtr<T>& Item : ArrayOfPtr)
	{
		// NOTE: Relying on the fact that the TObjectPtr will cache the resolved pointer in place after calling Get.
		Item.Get();
	}
	return reinterpret_cast<T**>(ArrayOfPtr);
}

template <typename T>
FORCEINLINE T** ToRawPtrArrayUnsafe(T** ArrayOfPtr)
{
	return ArrayOfPtr;
}



template <typename T>
FORCEINLINE TWeakObjectPtr<T> MakeWeakObjectPtr(TObjectPtr<T> Ptr)
{
	return TWeakObjectPtr<T>(Ptr);
}

FORCEINLINE TObjectPtr<UObject>& FObjectPtr::ToTObjectPtr()
{
	return *static_cast<TObjectPtr<UObject>*>(this);
}

FORCEINLINE const TObjectPtr<UObject>& FObjectPtr::ToTObjectPtr() const
{
	return *static_cast<const TObjectPtr<UObject>*>(this);
}

/** Exchange variants between TObjectPtr<T> and raw pointer to T */
template <typename T>
inline void Exchange(TObjectPtr<T>& A, T*& B)
{
	Swap((T*&)A, B);
}
template <typename T>
inline void Exchange(T*& A, TObjectPtr<T>& B)
{
	Swap(A, (T*&)B);
}
