// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Memory/MemoryView.h"
#include "Templates/Decay.h"
#include "Templates/EnableIf.h"
#include "Templates/SharedPointer.h"
#include "Templates/UnrealTemplate.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * A reference-counted shared buffer type.
 *
 * A shared buffer may either own its memory or provide a view into memory. When used as a view,
 * it is the responsibility of the user to guarantee that the lifetime of the wrapped memory will
 * exceed the lifetime of the shared buffer. \ref MakeSharedBufferOwned can be used to guarantee
 * that the memory is owned by the buffer.
 *
 * A shared buffer must be constructed using \ref MakeSharedBuffer to invoke one of the provided
 * constructors to allocate, assume ownership of, clone, or wrap a buffer.
 *
 * A shared buffer must be stored and accessed through one of its six reference types:
 * FSharedBuffer[Const]Ref or FSharedBuffer[Const][Weak]Ptr.
 */
class FSharedBuffer
{
public:
	enum EAssumeOwnershipTag { AssumeOwnership };
	enum ECloneTag { Clone };
	enum EWrapTag { Wrap };

	FSharedBuffer() = default;

	/** Use via MakeSharedBuffer. Allocate an owned buffer of the given size. */
	CORE_API explicit FSharedBuffer(uint64 Size);

	/** Use via MakeSharedBuffer. Assumes ownership of Data and calls FMemory::Free upon destruction. */
	CORE_API FSharedBuffer(EAssumeOwnershipTag, void* Data, uint64 Size);
	/** Use via MakeSharedBuffer. Clone the given buffer to an owned copy. */
	CORE_API FSharedBuffer(ECloneTag, const void* Data, uint64 Size);
	/** Use via MakeSharedBuffer. Wrap the given buffer without taking ownership. */
	CORE_API FSharedBuffer(EWrapTag, void* Data, uint64 Size);

	CORE_API ~FSharedBuffer();

	FSharedBuffer(const FSharedBuffer&) = delete;
	FSharedBuffer& operator=(const FSharedBuffer&) = delete;

	inline void* GetData() { return Data; }
	inline const void* GetData() const { return Data; }

	inline uint64 Size() const { return DataSize; }

	/** Whether the shared buffer owns the memory that it provides a view of. */
	inline bool IsOwned() const { return (Flags & uint16(ESharedBufferFlags::Owned)) != 0; }

	inline FMutableMemoryView GetView() { return FMutableMemoryView(GetData(), Size()); }
	inline FConstMemoryView GetView() const { return FConstMemoryView(GetData(), Size()); }

	inline operator FMutableMemoryView() { return GetView(); }
	inline operator FConstMemoryView() const { return GetView(); }

private:
	enum class ESharedBufferFlags : uint16
	{
		None = 0,
		Owned = 1 << 0,
	};

	void* Data = nullptr;
	union
	{
		uint64 DataSizeAndFlags = 0;
		struct
		{
			uint64 DataSize : 48;
			uint64 Flags : 16;
		};
	};
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FSharedBufferRef;
class FSharedBufferConstRef;

namespace SharedBufferPrivate
{
	/** Wrapper to allow GetTypeHash to resolve from within a scope with another overload. */
	template <typename... ArgTypes>
	auto WrapGetTypeHash(ArgTypes&&... Args) -> decltype(GetTypeHash(Forward<ArgTypes>(Args)...))
	{
		return GetTypeHash(Forward<ArgTypes>(Args)...);
	}

	FSharedBufferRef MakeSharedBuffer(TSharedRef<FSharedBuffer, ESPMode::ThreadSafe>&& Ref);
	FSharedBufferConstRef MakeSharedBuffer(TSharedRef<const FSharedBuffer, ESPMode::ThreadSafe>&& Ref);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The reference types defined below provide an interface that is a subset of the shared pointer
// types provided in Templates/SharedPointer.h. These types will be switched to alias the generic
// types once the generic types have been updated to always provide thread safety and to deprecate
// operations that are not safe in the presence of multi-threaded usage.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * A non-nullable, thread-safe, mutable reference to a shared buffer.
 *
 * Equivalent to TSharedRef<FSharedBuffer, ESPMode::ThreadSafe>
 */
class FSharedBufferRef
{
public:
	inline FSharedBuffer& Get() const { return Ref.Get(); }

	inline FSharedBuffer& operator*() const { return Ref.operator*(); }
	inline FSharedBuffer* operator->() const { return Ref.operator->(); }

	inline friend uint32 GetTypeHash(const FSharedBufferRef& InRef) { return SharedBufferPrivate::WrapGetTypeHash(InRef.Ref); }

private:
	constexpr inline friend bool IsValid(const FSharedBufferRef&) { return true; }

	inline explicit FSharedBufferRef(TSharedRef<FSharedBuffer, ESPMode::ThreadSafe>&& InRef) : Ref(InRef) {}

	friend FSharedBufferRef SharedBufferPrivate::MakeSharedBuffer(TSharedRef<FSharedBuffer, ESPMode::ThreadSafe>&& Ref);

	friend class FSharedBufferConstRef;
	friend class FSharedBufferPtr;
	friend class FSharedBufferConstPtr;
	friend class FSharedBufferWeakPtr;
	friend class FSharedBufferConstWeakPtr;

	TSharedRef<FSharedBuffer, ESPMode::ThreadSafe> Ref;
};

/**
 * A non-nullable, thread-safe, const reference to a shared buffer.
 *
 * Equivalent to TSharedRef<const FSharedBuffer, ESPMode::ThreadSafe>
 */
class FSharedBufferConstRef
{
public:
	inline FSharedBufferConstRef(const FSharedBufferRef& InRef) : Ref(InRef.Ref) {}

	inline const FSharedBuffer& Get() const { return Ref.Get(); }

	inline const FSharedBuffer& operator*() const { return Ref.operator*(); }
	inline const FSharedBuffer* operator->() const { return Ref.operator->(); }

	inline friend uint32 GetTypeHash(const FSharedBufferConstRef& InRef) { return SharedBufferPrivate::WrapGetTypeHash(InRef.Ref); }

private:
	constexpr inline friend bool IsValid(const FSharedBufferConstRef&) { return true; }

	inline explicit FSharedBufferConstRef(TSharedRef<const FSharedBuffer, ESPMode::ThreadSafe>&& InRef) : Ref(InRef) {}

	friend FSharedBufferConstRef SharedBufferPrivate::MakeSharedBuffer(TSharedRef<const FSharedBuffer, ESPMode::ThreadSafe>&& Ref);

	friend class FSharedBufferConstPtr;
	friend class FSharedBufferConstWeakPtr;

	TSharedRef<const FSharedBuffer, ESPMode::ThreadSafe> Ref;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * A non-nullable, thread-safe, mutable pointer to a shared buffer.
 *
 * Equivalent to TSharedPtr<FSharedBuffer, ESPMode::ThreadSafe>
 */
class FSharedBufferPtr
{
public:
	FSharedBufferPtr() = default;

	inline FSharedBufferPtr(const FSharedBufferRef& InRef) : Ptr(InRef.Ref) {}

	inline FSharedBufferRef ToSharedRef() const& { return FSharedBufferRef(Ptr.ToSharedRef()); }
	inline FSharedBufferRef ToSharedRef() && { return FSharedBufferRef(MoveTemp(Ptr).ToSharedRef()); }

	inline bool IsValid() const { return Ptr.IsValid(); }
	inline FSharedBuffer* Get() const { return Ptr.Get(); }

	inline explicit operator bool() const { return Ptr.IsValid(); }
	inline FSharedBuffer& operator*() const { return Ptr.operator*(); }
	inline FSharedBuffer* operator->() const { return Ptr.operator->(); }

	inline void Reset() { Ptr.Reset(); }

	inline friend uint32 GetTypeHash(const FSharedBufferPtr& InPtr) { return SharedBufferPrivate::WrapGetTypeHash(InPtr.Ptr); }

private:
	inline friend bool IsValid(const FSharedBufferPtr& InPtr) { return InPtr.IsValid(); }

	inline FSharedBufferPtr(TSharedPtr<FSharedBuffer, ESPMode::ThreadSafe>&& InPtr) : Ptr(InPtr) {}

	friend class FSharedBufferConstPtr;
	friend class FSharedBufferWeakPtr;
	friend class FSharedBufferConstWeakPtr;

	TSharedPtr<FSharedBuffer, ESPMode::ThreadSafe> Ptr;
};

/**
 * A non-nullable, thread-safe, const pointer to a shared buffer.
 *
 * Equivalent to TSharedPtr<const FSharedBuffer, ESPMode::ThreadSafe>
 */
class FSharedBufferConstPtr
{
public:
	FSharedBufferConstPtr() = default;

	inline FSharedBufferConstPtr(const FSharedBufferRef& InRef) : Ptr(InRef.Ref) {}
	inline FSharedBufferConstPtr(const FSharedBufferConstRef& InRef) : Ptr(InRef.Ref) {}
	inline FSharedBufferConstPtr(const FSharedBufferPtr& InPtr) : Ptr(InPtr.Ptr) {}
	inline FSharedBufferConstPtr(FSharedBufferPtr&& InPtr) : Ptr(MoveTemp(InPtr.Ptr)) {}

	inline FSharedBufferConstRef ToSharedRef() const& { return FSharedBufferConstRef(Ptr.ToSharedRef()); }
	inline FSharedBufferConstRef ToSharedRef() && { return FSharedBufferConstRef(MoveTemp(Ptr).ToSharedRef()); }

	inline bool IsValid() const { return Ptr.IsValid(); }
	inline const FSharedBuffer* Get() const { return Ptr.Get(); }

	inline explicit operator bool() const { return Ptr.IsValid(); }
	inline const FSharedBuffer& operator*() const { return Ptr.operator*(); }
	inline const FSharedBuffer* operator->() const { return Ptr.operator->(); }

	inline void Reset() { Ptr.Reset(); }

	inline friend uint32 GetTypeHash(const FSharedBufferConstPtr& InPtr) { return SharedBufferPrivate::WrapGetTypeHash(InPtr.Ptr); }

private:
	inline friend bool IsValid(const FSharedBufferConstPtr& InPtr) { return InPtr.IsValid(); }

	inline FSharedBufferConstPtr(TSharedPtr<const FSharedBuffer, ESPMode::ThreadSafe>&& InPtr) : Ptr(InPtr) {}

	friend class FSharedBufferConstWeakPtr;

	TSharedPtr<const FSharedBuffer, ESPMode::ThreadSafe> Ptr;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * A non-nullable, thread-safe, weak, mutable pointer to a shared buffer.
 *
 * Equivalent to TWeakPtr<FSharedBuffer, ESPMode::ThreadSafe>
 */
class FSharedBufferWeakPtr
{
public:
	FSharedBufferWeakPtr() = default;

	inline FSharedBufferWeakPtr(const FSharedBufferRef& InRef) : Ptr(InRef.Ref) {}
	inline FSharedBufferWeakPtr(const FSharedBufferPtr& InPtr) : Ptr(InPtr.Ptr) {}
	inline FSharedBufferWeakPtr(FSharedBufferPtr&& InPtr) : Ptr(MoveTemp(InPtr.Ptr)) {}

	inline FSharedBufferPtr Pin() const& { return FSharedBufferPtr(Ptr.Pin()); }
	inline FSharedBufferPtr Pin() && { return FSharedBufferPtr(MoveTemp(Ptr).Pin()); }

	inline void Reset() { Ptr.Reset(); }

	inline friend uint32 GetTypeHash(const FSharedBufferWeakPtr& InPtr) { return SharedBufferPrivate::WrapGetTypeHash(InPtr.Ptr); }

private:
	friend class FSharedBufferConstWeakPtr;

	TWeakPtr<FSharedBuffer, ESPMode::ThreadSafe> Ptr;
};

/**
 * A non-nullable, thread-safe, weak, const pointer to a shared buffer.
 *
 * Equivalent to TWeakPtr<const FSharedBuffer, ESPMode::ThreadSafe>
 */
class FSharedBufferConstWeakPtr
{
public:
	FSharedBufferConstWeakPtr() = default;

	inline FSharedBufferConstWeakPtr(const FSharedBufferRef& InRef) : Ptr(InRef.Ref) {}
	inline FSharedBufferConstWeakPtr(const FSharedBufferConstRef& InRef) : Ptr(InRef.Ref) {}
	inline FSharedBufferConstWeakPtr(const FSharedBufferPtr& InPtr) : Ptr(InPtr.Ptr) {}
	inline FSharedBufferConstWeakPtr(const FSharedBufferConstPtr& InPtr) : Ptr(InPtr.Ptr) {}
	inline FSharedBufferConstWeakPtr(const FSharedBufferWeakPtr& InPtr) : Ptr(InPtr.Ptr) {}
	inline FSharedBufferConstWeakPtr(FSharedBufferPtr&& InPtr) : Ptr(MoveTemp(InPtr.Ptr)) {}
	inline FSharedBufferConstWeakPtr(FSharedBufferConstPtr&& InPtr) : Ptr(MoveTemp(InPtr.Ptr)) {}
	inline FSharedBufferConstWeakPtr(FSharedBufferWeakPtr&& InPtr) : Ptr(MoveTemp(InPtr.Ptr)) {}

	inline FSharedBufferConstPtr Pin() const& { return FSharedBufferConstPtr(Ptr.Pin()); }
	inline FSharedBufferConstPtr Pin() && { return FSharedBufferConstPtr(MoveTemp(Ptr).Pin()); }

	inline void Reset() { Ptr.Reset(); }

	inline friend uint32 GetTypeHash(const FSharedBufferConstWeakPtr& InPtr) { return SharedBufferPrivate::WrapGetTypeHash(InPtr.Ptr); }

private:
	TWeakPtr<const FSharedBuffer, ESPMode::ThreadSafe> Ptr;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template<> struct TIsZeroConstructType<FSharedBufferRef> { enum { Value = true }; };
template<> struct TIsZeroConstructType<FSharedBufferConstRef> { enum { Value = true }; };
template<> struct TIsZeroConstructType<FSharedBufferPtr> { enum { Value = true }; };
template<> struct TIsZeroConstructType<FSharedBufferConstPtr> { enum { Value = true }; };
template<> struct TIsZeroConstructType<FSharedBufferWeakPtr> { enum { Value = true }; };
template<> struct TIsZeroConstructType<FSharedBufferConstWeakPtr> { enum { Value = true }; };

template<> struct TIsWeakPointerType<FSharedBufferWeakPtr> { enum { Value = true }; };
template<> struct TIsWeakPointerType<FSharedBufferConstWeakPtr> { enum { Value = true }; };

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace SharedBufferPrivate
{
	inline FSharedBufferRef MakeSharedBuffer(TSharedRef<FSharedBuffer, ESPMode::ThreadSafe>&& Ref)
	{
		return FSharedBufferRef(MoveTemp(Ref));
	}

	inline FSharedBufferConstRef MakeSharedBuffer(TSharedRef<const FSharedBuffer, ESPMode::ThreadSafe>&& Ref)
	{
		return FSharedBufferConstRef(MoveTemp(Ref));
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Construct a shared buffer. \see \ref FSharedBuffer for more details.
 *
 * MakeSharedBuffer(); an empty non-owned buffer
 * MakeSharedBuffer(uint64 Size); an uninitialized buffer of Size bytes
 * MakeSharedBuffer(FSharedBuffer::AssumeOwnership, Data, Size); take ownership of the buffer (Data, Size)
 * MakeSharedBuffer(FSharedBuffer::Clone, Data, Size); an owned clone the buffer (Data, Size)
 * MakeSharedBuffer(FSharedBuffer::Wrap, Data, Size); non-owning wrapper of the buffer (Data, Size)
 */
template <typename... ArgTypes, typename = decltype(FSharedBuffer(Forward<ArgTypes>(DeclVal<ArgTypes>())...))>
inline FSharedBufferRef MakeSharedBuffer(ArgTypes&&... Args)
{
	return SharedBufferPrivate::MakeSharedBuffer(MakeShared<FSharedBuffer, ESPMode::ThreadSafe>(Forward<ArgTypes>(Args)...));
}

/** Construct a const buffer to take ownership of the buffer (Data, Size). \see \ref FSharedBuffer for details. */
inline FSharedBufferConstRef MakeSharedBuffer(FSharedBuffer::EAssumeOwnershipTag, const void* Data, uint64 Size)
{
	// The const_cast is safe because it is returned as a const buffer that disallows mutable access.
	return SharedBufferPrivate::MakeSharedBuffer(MakeShared<const FSharedBuffer, ESPMode::ThreadSafe>(FSharedBuffer::AssumeOwnership, const_cast<void*>(Data), Size));
}

/** Construct a const non-owning wrapper of the buffer (Data, Size) \see \ref FSharedBuffer for details. */
inline FSharedBufferConstRef MakeSharedBuffer(FSharedBuffer::EWrapTag, const void* Data, uint64 Size)
{
	// The const_cast is safe because it is returned as a const buffer that disallows mutable access.
	return SharedBufferPrivate::MakeSharedBuffer(MakeShared<const FSharedBuffer, ESPMode::ThreadSafe>(FSharedBuffer::Wrap, const_cast<void*>(Data), Size));
}

/** Construct a shared buffer from a memory view. \see \ref FSharedBuffer for details. */
template <typename TagType, typename ViewType>
inline auto MakeSharedBuffer(TagType Tag, TMemoryView<ViewType> View) -> decltype(MakeSharedBuffer(DeclVal<TagType>(), View.GetData(), View.Size()))
{
	return MakeSharedBuffer(Tag, View.GetData(), View.Size());
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Return the buffer if it is owned, or a clone otherwise.
 *
 * @param Buffer A FSharedBuffer[Const]Ref or FSharedBuffer[Const][Weak]Ptr to make owned.
 * @return An owned copy of the input buffer in the same reference type as the input.
 */
template <typename T, typename TEnableIf<TIsSame<FSharedBuffer, typename TDecay<decltype(*DeclVal<T>())>::Type>::Value>::Type* = nullptr>
inline auto MakeSharedBufferOwned(T&& Buffer) -> decltype(typename TDecay<T>::Type(MakeSharedBuffer(FSharedBuffer::Clone, DeclVal<T>()->GetData(), DeclVal<T>()->Size())))
{
	return !IsValid(Buffer) || Buffer->IsOwned() ? Forward<T>(Buffer) : MakeSharedBuffer(FSharedBuffer::Clone, Buffer->GetData(), Buffer->Size());
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
