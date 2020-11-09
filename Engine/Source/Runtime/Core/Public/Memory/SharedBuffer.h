// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "HAL/UnrealMemory.h"
#include "Memory/MemoryView.h"
#include "Misc/EnumClassFlags.h"
#include "Templates/Invoke.h"
#include "Templates/TypeHash.h"
#include "Templates/UnrealTemplate.h"

#include <atomic>
#include <type_traits>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FSharedBuffer;

namespace SharedBufferPrivate
{

enum ESharedBufferFlags : uint16
{
	None = 0,
	Owned = 1 << 0,
	ReadOnly = 1 << 1,
	HasBufferOwner = 1 << 2,
};

ENUM_CLASS_FLAGS(ESharedBufferFlags);

struct FSharedRefOps final
{
	static bool TryAddRef(const FSharedBuffer* Buffer);
	static void AddRef(const FSharedBuffer* Buffer);
	static void Release(const FSharedBuffer* Buffer);
};

struct FSharedPtrOps final
{
	static bool TryAddRef(const FSharedBuffer* Buffer);
	static void AddRef(const FSharedBuffer* Buffer);
	static void Release(const FSharedBuffer* Buffer);
};

struct FWeakPtrOps final
{
	static bool TryAddRef(const FSharedBuffer* Buffer);
	static void AddRef(const FSharedBuffer* Buffer);
	static void Release(const FSharedBuffer* Buffer);
};

template <bool bInAllowNull, bool bInIsConst, bool bInIsWeak>
class TSharedBufferPtr final
{
	static constexpr bool bAllowNull = bInAllowNull;
	static constexpr bool bIsConst = bInIsConst;
	static constexpr bool bIsWeak = bInIsWeak;
	using BufferType = std::conditional_t<bIsConst, const FSharedBuffer, FSharedBuffer>;
	using Ops = std::conditional_t<bIsWeak, FWeakPtrOps, std::conditional_t<bAllowNull, FSharedPtrOps, FSharedRefOps>>;

	template <bool bOtherAllowNull, bool bOtherIsConst, bool bOtherIsWeak>
	friend class TSharedBufferPtr;

	template <bool bOtherAllowNull, bool bOtherIsConst, bool bOtherIsWeak>
	static inline BufferType* CopyFrom(const TSharedBufferPtr<bOtherAllowNull, bOtherIsConst, bOtherIsWeak>& Ptr)
	{
		BufferType* NewBuffer = Ptr.Buffer;
		if (bOtherIsWeak && !bIsWeak)
		{
			if (!Ops::TryAddRef(NewBuffer))
			{
				NewBuffer = nullptr;
			}
		}
		else
		{
			Ops::AddRef(NewBuffer);
		}
		return NewBuffer;
	}

	template <bool bOtherAllowNull, bool bOtherIsConst, bool bOtherIsWeak>
	static inline BufferType* MoveFrom(TSharedBufferPtr<bOtherAllowNull, bOtherIsConst, bOtherIsWeak>&& Ptr)
	{
		BufferType* NewBuffer = Ptr.Buffer;
		if (bOtherIsWeak)
		{
			if (bIsWeak)
			{
				Ptr.Buffer = nullptr;
			}
			else
			{
				if (!Ops::TryAddRef(NewBuffer))
				{
					NewBuffer = nullptr;
				}
			}
		}
		else
		{
			if (bIsWeak)
			{
				Ops::AddRef(NewBuffer);
			}
			else
			{
				if (bOtherAllowNull)
				{
					Ptr.Buffer = nullptr;
				}
				else
				{
					Ops::AddRef(NewBuffer);
				}
			}
		}
		return NewBuffer;
	}

public:
	inline explicit TSharedBufferPtr(BufferType* const InBuffer)
		: Buffer(InBuffer)
	{
		Ops::AddRef(InBuffer);
	}

	inline TSharedBufferPtr(const TSharedBufferPtr& Ptr)
		: Buffer(CopyFrom(Ptr))
	{
	}

	inline TSharedBufferPtr(TSharedBufferPtr&& Ptr)
		: Buffer(MoveFrom(MoveTemp(Ptr)))
	{
	}

	template <bool bOtherAllowNull, bool bOtherIsConst, bool bOtherIsWeak>
	inline explicit TSharedBufferPtr(const TSharedBufferPtr<bOtherAllowNull, bOtherIsConst, bOtherIsWeak>& Ptr)
		: Buffer(CopyFrom(Ptr))
	{
	}

	template <bool bOtherAllowNull, bool bOtherIsConst, bool bOtherIsWeak>
	inline explicit TSharedBufferPtr(TSharedBufferPtr<bOtherAllowNull, bOtherIsConst, bOtherIsWeak>&& Ptr)
		: Buffer(MoveFrom(MoveTemp(Ptr)))
	{
	}

	inline ~TSharedBufferPtr()
	{
		Ops::Release(Buffer);
	}

	inline TSharedBufferPtr& operator=(const TSharedBufferPtr& Ptr)
	{
		BufferType* const OldBuffer = Buffer;
		Buffer = CopyFrom(Ptr);
		Ops::Release(OldBuffer);
		return *this;
	}

	inline TSharedBufferPtr& operator=(TSharedBufferPtr&& Ptr)
	{
		BufferType* const OldBuffer = Buffer;
		Buffer = MoveFrom(MoveTemp(Ptr));
		Ops::Release(OldBuffer);
		return *this;
	}

	template <bool bOtherAllowNull, bool bOtherIsConst, bool bOtherIsWeak>
	inline TSharedBufferPtr& operator=(const TSharedBufferPtr<bOtherAllowNull, bOtherIsConst, bOtherIsWeak>& Ptr)
	{
		BufferType* const OldBuffer = Buffer;
		Buffer = CopyFrom(Ptr);
		Ops::Release(OldBuffer);
		return *this;
	}

	template <bool bOtherAllowNull, bool bOtherIsConst, bool bOtherIsWeak>
	inline TSharedBufferPtr& operator=(TSharedBufferPtr<bOtherAllowNull, bOtherIsConst, bOtherIsWeak>&& Ptr)
	{
		BufferType* const OldBuffer = Buffer;
		Buffer = MoveFrom(MoveTemp(Ptr));
		Ops::Release(OldBuffer);
		return *this;
	}

	BufferType* Get() const { return Buffer; }

	template <bool bOtherAllowNull, bool bOtherIsConst, bool bOtherIsWeak>
	inline bool operator==(const TSharedBufferPtr<bOtherAllowNull, bOtherIsConst, bOtherIsWeak>& Ptr) const
	{
		return Buffer == Ptr.Buffer;
	}

	template <bool bOtherAllowNull, bool bOtherIsConst, bool bOtherIsWeak>
	inline bool operator!=(const TSharedBufferPtr<bOtherAllowNull, bOtherIsConst, bOtherIsWeak>& Ptr) const
	{
		return Buffer != Ptr.Buffer;
	}

private:
	BufferType* Buffer = nullptr;
};

}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** A non-nullable, thread-safe, reference to a writable shared buffer. */
class FSharedBufferRef final
{
public:
	using PtrType = SharedBufferPrivate::TSharedBufferPtr</*bAllowNull*/ false, /*bIsConst*/ false, /*bIsWeak*/ false>;

	inline explicit FSharedBufferRef(PtrType&& InPtr) : Ptr(MoveTemp(InPtr)) {}

	inline FSharedBuffer& Get() const { return *Ptr.Get(); }

	inline FSharedBuffer& operator*() const { return *Ptr.Get(); }
	inline FSharedBuffer* operator->() const { return Ptr.Get(); }

	inline friend uint32 GetTypeHash(const FSharedBufferRef& Ref) { return PointerHash(Ref.Ptr.Get()); }

private:
	constexpr inline friend bool IsValid(const FSharedBufferRef&) { return true; }
	inline friend FSharedBufferRef ToSharedRef(const FSharedBufferRef& Ref) { return Ref; }
	inline friend const PtrType& ToPrivateSharedBufferPtr(const FSharedBufferRef& Ref) { return Ref.Ptr; }

	friend class FSharedBufferConstRef;
	friend class FSharedBufferPtr;
	friend class FSharedBufferConstPtr;
	friend class FSharedBufferWeakPtr;
	friend class FSharedBufferConstWeakPtr;

	PtrType Ptr;
};

/** A non-nullable, thread-safe, const reference to a shared buffer. */
class FSharedBufferConstRef final
{
public:
	using PtrType = SharedBufferPrivate::TSharedBufferPtr</*bAllowNull*/ false, /*bIsConst*/ true, /*bIsWeak*/ false>;

	inline explicit FSharedBufferConstRef(PtrType&& InPtr) : Ptr(MoveTemp(InPtr)) {}

	inline FSharedBufferConstRef(const FSharedBufferRef& Ref) : Ptr(Ref.Ptr) {}

	inline const FSharedBuffer& Get() const { return *Ptr.Get(); }

	inline const FSharedBuffer& operator*() const { return *Ptr.Get(); }
	inline const FSharedBuffer* operator->() const { return Ptr.Get(); }

	inline friend uint32 GetTypeHash(const FSharedBufferConstRef& Ref) { return PointerHash(Ref.Ptr.Get()); }

private:
	constexpr inline friend bool IsValid(const FSharedBufferConstRef&) { return true; }
	inline friend FSharedBufferConstRef ToSharedRef(const FSharedBufferConstRef& Ref) { return Ref; }
	inline friend const PtrType& ToPrivateSharedBufferPtr(const FSharedBufferConstRef& Ref) { return Ref.Ptr; }

	friend class FSharedBufferConstPtr;
	friend class FSharedBufferConstWeakPtr;

	PtrType Ptr;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** A non-nullable, thread-safe, pointer to a writable shared buffer. */
class FSharedBufferPtr final
{
public:
	using PtrType = SharedBufferPrivate::TSharedBufferPtr</*bAllowNull*/ true, /*bIsConst*/ false, /*bIsWeak*/ false>;

	inline explicit FSharedBufferPtr(PtrType&& InPtr) : Ptr(MoveTemp(InPtr)) {}

	inline FSharedBufferPtr() : Ptr(nullptr) {}
	inline FSharedBufferPtr(const FSharedBufferRef& Ref) : Ptr(Ref.Ptr) {}

	inline FSharedBufferRef ToSharedRef() const& { return FSharedBufferRef(FSharedBufferRef::PtrType(Ptr)); }
	inline FSharedBufferRef ToSharedRef() && { return FSharedBufferRef(FSharedBufferRef::PtrType(MoveTemp(Ptr))); }

	inline bool IsValid() const { return Ptr.Get() != nullptr; }
	inline FSharedBuffer* Get() const { return Ptr.Get(); }

	inline explicit operator bool() const { return Ptr.Get() != nullptr; }
	inline FSharedBuffer& operator*() const { return *Ptr.Get(); }
	inline FSharedBuffer* operator->() const { return Ptr.Get(); }

	inline void Reset() { *this = FSharedBufferPtr(); }

	inline friend uint32 GetTypeHash(const FSharedBufferPtr& InPtr) { return PointerHash(InPtr.Ptr.Get()); }

private:
	inline friend bool IsValid(const FSharedBufferPtr& InPtr) { return InPtr.IsValid(); }
	inline friend FSharedBufferRef ToSharedRef(const FSharedBufferPtr& InPtr) { return InPtr.ToSharedRef(); }
	inline friend FSharedBufferRef ToSharedRef(FSharedBufferPtr&& InPtr) { return MoveTemp(InPtr).ToSharedRef(); }
	inline friend const PtrType& ToPrivateSharedBufferPtr(const FSharedBufferPtr& InPtr) { return InPtr.Ptr; }

	friend class FSharedBufferConstPtr;
	friend class FSharedBufferWeakPtr;
	friend class FSharedBufferConstWeakPtr;

	PtrType Ptr;
};

/** A non-nullable, thread-safe, const pointer to a shared buffer. */
class FSharedBufferConstPtr final
{
public:
	using PtrType = SharedBufferPrivate::TSharedBufferPtr</*bAllowNull*/ true, /*bIsConst*/ true, /*bIsWeak*/ false>;

	inline explicit FSharedBufferConstPtr(PtrType&& InPtr) : Ptr(MoveTemp(InPtr)) {}

	inline FSharedBufferConstPtr() : Ptr(nullptr) {}
	inline FSharedBufferConstPtr(const FSharedBufferRef& Ref) : Ptr(Ref.Ptr) {}
	inline FSharedBufferConstPtr(const FSharedBufferConstRef& Ref) : Ptr(Ref.Ptr) {}
	inline FSharedBufferConstPtr(const FSharedBufferPtr& InPtr) : Ptr(InPtr.Ptr) {}
	inline FSharedBufferConstPtr(FSharedBufferPtr&& InPtr) : Ptr(MoveTemp(InPtr.Ptr)) {}

	inline FSharedBufferConstRef ToSharedRef() const& { return FSharedBufferConstRef(FSharedBufferConstRef::PtrType(Ptr)); }
	inline FSharedBufferConstRef ToSharedRef() && { return FSharedBufferConstRef(FSharedBufferConstRef::PtrType(MoveTemp(Ptr))); }

	inline bool IsValid() const { return Ptr.Get() != nullptr; }
	inline const FSharedBuffer* Get() const { return Ptr.Get(); }

	inline explicit operator bool() const { return Ptr.Get() != nullptr; }
	inline const FSharedBuffer& operator*() const { return *Ptr.Get(); }
	inline const FSharedBuffer* operator->() const { return Ptr.Get(); }

	inline void Reset() { *this = FSharedBufferConstPtr(); }

	inline friend uint32 GetTypeHash(const FSharedBufferConstPtr& InPtr) { return PointerHash(InPtr.Ptr.Get()); }

private:
	inline friend bool IsValid(const FSharedBufferConstPtr& InPtr) { return InPtr.IsValid(); }
	inline friend FSharedBufferConstRef ToSharedRef(const FSharedBufferConstPtr& InPtr) { return InPtr.ToSharedRef(); }
	inline friend FSharedBufferConstRef ToSharedRef(FSharedBufferConstPtr&& InPtr) { return MoveTemp(InPtr).ToSharedRef(); }
	inline friend const PtrType& ToPrivateSharedBufferPtr(const FSharedBufferConstPtr& InPtr) { return InPtr.Ptr; }

	friend class FSharedBufferConstWeakPtr;

	PtrType Ptr;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** A non-nullable, thread-safe, weak, pointer to a writable shared buffer. */
class FSharedBufferWeakPtr final
{
public:
	using PtrType = SharedBufferPrivate::TSharedBufferPtr</*bAllowNull*/ true, /*bIsConst*/ false, /*bIsWeak*/ true>;

	inline explicit FSharedBufferWeakPtr(PtrType&& InPtr) : Ptr(MoveTemp(InPtr)) {}

	inline FSharedBufferWeakPtr() : Ptr(nullptr) {}
	inline FSharedBufferWeakPtr(const FSharedBufferRef& Ref) : Ptr(Ref.Ptr) {}
	inline FSharedBufferWeakPtr(const FSharedBufferPtr& InPtr) : Ptr(InPtr.Ptr) {}

	inline FSharedBufferPtr Pin() const { return FSharedBufferPtr(FSharedBufferPtr::PtrType(Ptr)); }

	inline void Reset() { *this = FSharedBufferWeakPtr(); }

	inline friend uint32 GetTypeHash(const FSharedBufferWeakPtr& InPtr) { return PointerHash(InPtr.Ptr.Get()); }

private:
	friend class FSharedBufferConstWeakPtr;
	inline friend const PtrType& ToPrivateSharedBufferPtr(const FSharedBufferWeakPtr& InPtr) { return InPtr.Ptr; }

	PtrType Ptr;
};

/** A non-nullable, thread-safe, weak, const pointer to a shared buffer. */
class FSharedBufferConstWeakPtr final
{
public:
	using PtrType = SharedBufferPrivate::TSharedBufferPtr</*bAllowNull*/ true, /*bIsConst*/ true, /*bIsWeak*/ true>;

	inline explicit FSharedBufferConstWeakPtr(PtrType&& InPtr) : Ptr(MoveTemp(InPtr)) {}

	inline FSharedBufferConstWeakPtr() : Ptr(nullptr) {}
	inline FSharedBufferConstWeakPtr(const FSharedBufferRef& Ref) : Ptr(Ref.Ptr) {}
	inline FSharedBufferConstWeakPtr(const FSharedBufferConstRef& Ref) : Ptr(Ref.Ptr) {}
	inline FSharedBufferConstWeakPtr(const FSharedBufferPtr& InPtr) : Ptr(InPtr.Ptr) {}
	inline FSharedBufferConstWeakPtr(const FSharedBufferConstPtr& InPtr) : Ptr(InPtr.Ptr) {}
	inline FSharedBufferConstWeakPtr(const FSharedBufferWeakPtr& InPtr) : Ptr(InPtr.Ptr) {}
	inline FSharedBufferConstWeakPtr(FSharedBufferWeakPtr&& InPtr) : Ptr(MoveTemp(InPtr.Ptr)) {}

	inline FSharedBufferConstPtr Pin() const { return FSharedBufferConstPtr(FSharedBufferConstPtr::PtrType(Ptr)); }

	inline void Reset() { *this = FSharedBufferConstWeakPtr(); }

	inline friend uint32 GetTypeHash(const FSharedBufferConstWeakPtr& InPtr) { return PointerHash(InPtr.Ptr.Get()); }

private:
	inline friend const PtrType& ToPrivateSharedBufferPtr(const FSharedBufferConstWeakPtr& InPtr) { return InPtr.Ptr; }

	PtrType Ptr;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template <typename TypeA, typename TypeB>
inline auto operator==(const TypeA& BufferA, const TypeB& BufferB)
	-> decltype(ToPrivateSharedBufferPtr(BufferA) == ToPrivateSharedBufferPtr(BufferB))
{
	return ToPrivateSharedBufferPtr(BufferA) == ToPrivateSharedBufferPtr(BufferB);
}

template <typename TypeA, typename TypeB>
inline auto operator!=(const TypeA& BufferA, const TypeB& BufferB)
	-> decltype(ToPrivateSharedBufferPtr(BufferA) != ToPrivateSharedBufferPtr(BufferB))
{
	return ToPrivateSharedBufferPtr(BufferA) != ToPrivateSharedBufferPtr(BufferB);
}

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

/**
 * A reference-counted shared buffer type.
 *
 * A shared buffer may either own its memory or provide a view into memory owned externally. When
 * used as to wrap a externally-owned memory, the user must guarantee the lifetime of the wrapped
 * memory will exceed the lifetime of the shared buffer.
 *
 * A shared buffer must be stored and accessed through one of its six reference types:
 * FSharedBuffer[Const]Ref or FSharedBuffer[Const][Weak]Ptr.
 *
 * Make an owned sized buffer with optional alignment.
 *
 *     FSharedBuffer::Alloc(128)
 *     FSharedBuffer::Alloc(128, 16)
 *
 * Make an owned buffer by cloning the buffer pointed to by the arguments.
 *
 *     FSharedBuffer::Clone(Data, Size)
 *     FSharedBuffer::Clone(MakeMemoryView(...))
 *     FSharedBuffer::Clone(*SharedBufferRefOrPtr)
 *
 * Make a non-owned buffer by creating a view of the buffer pointed to by the arguments.
 *
 *     FSharedBuffer::MakeView(Data, Size)
 *     FSharedBuffer::MakeView(MakeMemoryView(...))
 *     FSharedBuffer::MakeView(Data, Size, OuterBuffer)
 *     FSharedBuffer::MakeView(MakeMemoryView(...), OuterBuffer)
 *
 * Make an owned buffer by taking ownership of the buffer pointed to by the arguments.
 *
 *     FSharedBuffer::TakeOwnership(Data, Size, FMemory::Free)
 *     FSharedBuffer::TakeOwnership(Data, Size, [](void* Data) { delete[] static_cast<uint8*>(Data); })
 *     FSharedBuffer::TakeOwnership(Data, Size, FCustomBufferOwner())
 */
class FSharedBuffer final
{
	using ESharedBufferFlags = SharedBufferPrivate::ESharedBufferFlags;

public:
	/**
	 * Base for customizing lifetime management for the buffer.
	 *
	 * Types derived from this must be movable. Prefer an arbitrary callable (including a lambda) to
	 * handle deletion in place of the extra code required to derive from this type.
	 */
	class FBufferOwner
	{
	protected:
		FBufferOwner() = default;

	public:
		virtual ~FBufferOwner() = default;

		/** Free the memory for the buffer. */
		virtual void Free(void* Data, uint64 Size) = 0;
	};

	/** Make an owned writable buffer of Size bytes. */
	static inline FSharedBufferPtr Alloc(const uint64 Size, const uint32 Alignment = DEFAULT_ALIGNMENT)
	{
		return NewBuffer(FMemory::Malloc(Size, Alignment), Size, ESharedBufferFlags::Owned);
	}

	/** Make an owned writable clone of the memory view. */
	static inline FSharedBufferPtr Clone(const FConstMemoryView View)
	{
		return Clone(View.GetData(), View.GetSize());
	}

	/** Make an owned writable clone of the buffer. */
	static inline FSharedBufferPtr Clone(const void* const Data, const uint64 Size)
	{
		const FSharedBufferPtr Buffer = Alloc(Size);
		FMemory::Memcpy(Buffer->GetData(), Data, Size);
		return Buffer;
	}

	/** Make a non-owned writable view of the memory view. */
	static inline FSharedBufferPtr MakeView(FMutableMemoryView View)
	{
		return MakeView(View.GetData(), View.GetSize());
	}

	/** Make a non-owned read-only view of the memory view. */
	static inline FSharedBufferConstPtr MakeView(FConstMemoryView View)
	{
		return MakeView(View.GetData(), View.GetSize());
	}

	/** Make a non-owned writable view of the buffer. */
	static inline FSharedBufferPtr MakeView(void* Data, uint64 Size)
	{
		return NewBuffer(Data, Size, ESharedBufferFlags::None);
	}

	/** Make a non-owned read-only view of the buffer. */
	static inline FSharedBufferConstPtr MakeView(const void* Data, uint64 Size)
	{
		// const_cast is safe here because the return value only allows accessing data through a const void*
		return MakeView(const_cast<void*>(Data), Size);
	}

	/** Make a non-owned read-only view of the outer buffer and hold a reference to it. */
	static inline FSharedBufferConstPtr MakeView(FConstMemoryView View, const FSharedBuffer& OuterBuffer)
	{
		return MakeView(View.GetData(), View.GetSize(), OuterBuffer);
	}

	/** Make a non-owned read-only view of the outer buffer and hold a reference to it. */
	static inline FSharedBufferConstPtr MakeView(const void* Data, uint64 Size, const FSharedBuffer& OuterBuffer)
	{
		check(OuterBuffer.GetView().Contains(MakeMemoryView(Data, Size)));
		// Create a reference now to prevent the read-only state from changing before we add a reference later.
		FSharedBufferConstPtr OuterBufferPtr{FSharedBufferConstPtr::PtrType(&OuterBuffer)};
		// This buffer is not owned, but is read-only if its outer is read-only.
		const ESharedBufferFlags Flags = GetFlags(OuterBuffer.ReferenceCountAndFlags) & ESharedBufferFlags::ReadOnly;
		// const_cast is safe here because the return value only allows accessing data through a const void*
		return NewBufferWithOwner<FOuterBufferOwner>(const_cast<void*>(Data), Size, Flags, MoveTemp(OuterBufferPtr));
	}

	/**
	 * Make an owned writable buffer by taking ownership of the provided memory.
	 *
	 * @param DeleteFunction Called with the data pointer to free memory when there are no shared references.
	 */
	template <typename DeleteFunctionType,
		decltype(Invoke(std::declval<DeleteFunctionType>(), std::declval<void*>()))* = nullptr>
	static inline FSharedBufferPtr TakeOwnership(void* Data, uint64 Size, DeleteFunctionType&& DeleteFunction)
	{
		return NewBufferWithOwner<TDeleteFunctionOwner<DeleteFunctionType>>(Data, Size, ESharedBufferFlags::Owned,
			Forward<DeleteFunctionType>(DeleteFunction));
	}

	/**
	 * Make an owned read-only buffer by taking ownership of the provided memory.
	 *
	 * @param DeleteFunction Called with the data pointer to free memory when there are no shared references.
	 */
	template <typename DeleteFunctionType,
		decltype(Invoke(std::declval<DeleteFunctionType>(), std::declval<void*>()))* = nullptr>
	static inline FSharedBufferConstPtr TakeOwnership(const void* Data, uint64 Size, DeleteFunctionType&& DeleteFunction)
	{
		// const_cast is safe here because the return value only allows accessing data through a const void*
		return TakeOwnership(const_cast<void*>(Data), Size, Forward<DeleteFunctionType>(DeleteFunction));
	}

	/**
	 * Make an owned read-only buffer by taking ownership of the provided memory.
	 *
	 * @param BufferOwner An owner for the buffer that is derived from FBufferOwner.
	 */
	template <typename BufferOwnerType,
		decltype(ImplicitConv<FBufferOwner*>(std::declval<std::decay_t<BufferOwnerType>*>()))* = nullptr>
	static inline FSharedBufferPtr TakeOwnership(void* Data, uint64 Size, BufferOwnerType&& BufferOwner)
	{
		return NewBufferWithOwner<BufferOwnerType>(Data, Size, ESharedBufferFlags::Owned, Forward<BufferOwnerType>(BufferOwner));
	}

	/**
	 * Make an owned read-only buffer by taking ownership of the provided memory.
	 *
	 * @param BufferOwner An owner for the buffer that is derived from FBufferOwner.
	 */
	template <typename BufferOwnerType,
		decltype(&ImplicitConv<FBufferOwner*>(std::declval<std::decay_t<BufferOwnerType>*>()))* = nullptr>
	static inline FSharedBufferConstPtr TakeOwnership(const void* Data, uint64 Size, BufferOwnerType&& BufferOwner)
	{
		// const_cast is safe here because the return value only allows accessing data through a const void*
		return TakeOwnership(const_cast<void*>(Data), Size, Forward<BufferOwnerType>(BufferOwner));
	}

	/**
	 * Return the buffer if it is owned, or a clone otherwise.
	 *
	 * @param Buffer A FSharedBuffer[Const]Ref or FSharedBuffer[Const]Ptr to make owned.
	 * @return An owned copy of the input buffer in the same reference type as the input.
	 */
	template <typename T,
		std::enable_if_t<std::is_same<FSharedBuffer, std::decay_t<decltype(*std::declval<T>())>>::value>* = nullptr
		>
	static inline auto MakeOwned(T&& Buffer) -> decltype(std::decay_t<T>(ToSharedRef(Clone(Buffer->GetData(), Buffer->GetSize()))))
	{
		return !IsValid(Buffer) || Buffer->IsOwned() ? Forward<T>(Buffer) : ToSharedRef(Clone(Buffer->GetData(), Buffer->GetSize()));
	}

	/** Return the buffer if it is owned and either read-only or this is the only reference, or a clone otherwise. */
	static inline FSharedBufferConstRef MakeReadOnly(FSharedBufferConstRef&& Buffer)
	{
		return Buffer->TryMakeReadOnly() ? MoveTemp(Buffer) : ToSharedRef(MakeReadOnly(Clone(*Buffer)));
	}

	/** Return the buffer if it is owned and either read-only or this is the only reference, or a clone otherwise. */
	static inline FSharedBufferConstPtr MakeReadOnly(FSharedBufferPtr&& Buffer)
	{
		return MakeReadOnly(FSharedBufferConstPtr(MoveTemp(Buffer)));
	}

	/** Return the buffer if it is owned and either read-only or this is the only reference, or a clone otherwise. */
	static inline FSharedBufferConstPtr MakeReadOnly(FSharedBufferConstPtr&& Buffer)
	{
		return Buffer ? Buffer->TryMakeReadOnly() ? MoveTemp(Buffer) : MakeReadOnly(Clone(*Buffer)) : FSharedBufferConstPtr();
	}

	// Disable overloads that would allow the argument to be copied.
	static void MakeReadOnly(FSharedBufferRef&&) = delete;
	static void MakeReadOnly(const FSharedBufferRef&) = delete;
	static void MakeReadOnly(const FSharedBufferConstRef&) = delete;
	static void MakeReadOnly(const FSharedBufferPtr&) = delete;
	static void MakeReadOnly(const FSharedBufferConstPtr&) = delete;

	/** Return the buffer because it is already writable. */
	static inline FSharedBufferRef MakeWritable(FSharedBufferRef&& Buffer) { return MoveTemp(Buffer); }

	/** Return the buffer if it is owned and either writable or this is the only reference, or a clone otherwise. */
	static inline FSharedBufferRef MakeWritable(FSharedBufferConstRef&& Buffer)
	{
		if (Buffer->TryMakeWritable())
		{
			return FSharedBufferRef(FSharedBufferRef::PtrType(const_cast<FSharedBuffer*>(&Buffer.Get())));
		}
		return Clone(*Buffer).ToSharedRef();
	}

	/** Return the buffer because it is already writable. */
	static inline FSharedBufferPtr MakeWritable(FSharedBufferPtr&& Buffer) { return MoveTemp(Buffer); }

	/** Return the buffer if it is owned and either writable or this is the only reference, or a clone otherwise. */
	static inline FSharedBufferPtr MakeWritable(FSharedBufferConstPtr&& Buffer)
	{
		if (Buffer->TryMakeWritable())
		{
			return FSharedBufferPtr(FSharedBufferPtr::PtrType(const_cast<FSharedBuffer*>(Buffer.Get())));
		}
		return Clone(*Buffer);
	}

	// Disable overloads that would allow the argument to be copied.
	static void MakeWritable(const FSharedBufferRef&) = delete;
	static void MakeWritable(const FSharedBufferConstRef&) = delete;
	static void MakeWritable(const FSharedBufferPtr&) = delete;
	static void MakeWritable(const FSharedBufferConstPtr&) = delete;

	/** A pointer to the start of the buffer. */
	inline void* GetData() { return Data; }
	/** A pointer to the start of the buffer. */
	inline const void* GetData() const { return Data; }

	/** The size of the buffer in bytes. */
	inline uint64 GetSize() const { return Size; }

	/** Whether this shared buffer owns the memory that it provides a view of. */
	inline bool IsOwned() const
	{
		return EnumHasAnyFlags(GetFlags(ReferenceCountAndFlags), ESharedBufferFlags::Owned);
	}

	/**
	 * Whether the shared buffer is read-only.
	 *
	 * A read-only shared buffer (or its outer) is owned and every reference to it is const.
	 */
	inline bool IsReadOnly() const
	{
		return EnumHasAnyFlags(GetFlags(ReferenceCountAndFlags), ESharedBufferFlags::ReadOnly);
	}

	/**
	 * Try to make the shared buffer read-only.
	 *
	 * A shared buffer can be made read-only if it is owned and there is only one reference to it.
	 *
	 * @return true if the buffer was read-only or was made read-only, otherwise false.
	 */
	CORE_API bool TryMakeReadOnly() const;

	// Disable this overload because it would allow a read-only buffer to be written.
	bool TryMakeReadOnly() = delete;

	/**
	 * Try to make the shared buffer writable.
	 *
	 * A shared buffer can be made writable if it is owned and there is only one reference to it.
	 *
	 * @return true if the buffer was writable or was made writable, otherwise false.
	 */
	CORE_API bool TryMakeWritable() const;

	/** The shared buffer is already writable. */
	inline bool TryMakeWritable() { return true; }

	/** A writable view of buffer. */
	inline FMutableMemoryView GetView() { return FMutableMemoryView(GetData(), GetSize()); }
	/** A read-only view of the buffer. */
	inline FConstMemoryView GetView() const { return FConstMemoryView(GetData(), GetSize()); }

	inline operator FMutableMemoryView() { return GetView(); }
	inline operator FConstMemoryView() const { return GetView(); }

private:
	/**
	 * Construct a new shared buffer with a buffer owner.
	 *
	 * @param Data A pointer to the start of the data buffer.
	 * @param Size The size of the data buffer.
	 * @param Flags The flags associated with the data buffer. Internally adds HasBufferOwner.
	 * @param OwnerArgs Arguments to the constructor of the buffer owner.
	 */
	template <typename OwnerType, typename... OwnerArgTypes>
	static inline FSharedBufferRef NewBufferWithOwner(
		void* const Data,
		const uint64 Size,
		ESharedBufferFlags Flags,
		OwnerArgTypes&&... OwnerArgs)
	{
		Flags |= ESharedBufferFlags::HasBufferOwner;
		static_assert(alignof(OwnerType) <= alignof(FSharedBuffer), "Required alignment of OwnerType is too high.");
		FSharedBufferRef Buffer = NewBuffer(Data, Size, Flags, sizeof(OwnerType));
		new(&Buffer.Get() + 1) OwnerType(Forward<OwnerArgTypes>(OwnerArgs)...);
		return Buffer;
	}

	/**
	 * Construct a new shared buffer.
	 *
	 * @param Data A pointer to the start of the data buffer.
	 * @param Size The size of the data buffer.
	 * @param Flags The flags associated with the data buffer.
	 * @param OwnerSize The size of the optional buffer owner to allocate after the shared buffer.
	 */
	CORE_API static FSharedBufferRef NewBuffer(void* Data, uint64 Size, ESharedBufferFlags Flags, uint32 OwnerSize = 0);

	/**
	 * Delete a shared buffer. Requires ReleaseData to have been called already.
	 */
	CORE_API static void DeleteBuffer(FSharedBuffer* Buffer);

	/**
	 * Free the data if it is owned and release the pointer to it.
	 *
	 * This does not delete the shared buffer object, which survives until the last shared or weak
	 * reference to it has been released.
	 */
	CORE_API void ReleaseData();

	FSharedBuffer() = default;
	FSharedBuffer(const FSharedBuffer&) = delete;
	FSharedBuffer& operator=(const FSharedBuffer&) = delete;
	~FSharedBuffer() = default;

	/** A buffer owner that holds a reference to an outer shared buffer. */
	class FOuterBufferOwner final : public FBufferOwner
	{
	public:
		explicit FOuterBufferOwner(FSharedBufferConstPtr&& InOuterBuffer)
			: OuterBuffer(MoveTemp(InOuterBuffer))
		{
		}

	private:
		virtual void Free(void* InData, uint64 InSize) final
		{
			// Destruction of the OuterBuffer reference will release the buffer.
		}

		FSharedBufferConstPtr OuterBuffer;
	};

	/** A buffer owner that wraps an arbitrary callable delete function. */
	template <typename DeleteFunctionType>
	class TDeleteFunctionOwner final : public FBufferOwner
	{
	public:
		explicit TDeleteFunctionOwner(DeleteFunctionType&& InDeleteFunction)
			: InDeleteFunction(Forward<DeleteFunctionType>(InDeleteFunction))
		{
		}

	private:
		virtual void Free(void* InData, uint64 InSize) final
		{
			Invoke(InDeleteFunction, InData);
		}

		std::decay_t<DeleteFunctionType> InDeleteFunction;
	};

private:
	static uint32 GetSharedRefCount(uint64 RefCountsAndFlags) { return uint32(RefCountsAndFlags >> 0) & 0xffffff; }
	static uint64 SetSharedRefCount(uint32 RefCount) { return uint64(RefCount) << 0; }
	static uint32 GetWeakRefCount(uint64 RefCountsAndFlags) { return uint32(RefCountsAndFlags >> 24) & 0xffffff; }
	static uint64 SetWeakRefCount(uint32 RefCount) { return uint64(RefCount) << 24; }
	static ESharedBufferFlags GetFlags(uint64 RefCountsAndFlags) { return ESharedBufferFlags(RefCountsAndFlags >> 48); }
	static uint64 SetFlags(ESharedBufferFlags Flags) { return uint64(Flags) << 48; }

	inline void AddSharedReference() const
	{
		const uint64 PreviousValue = ReferenceCountAndFlags.fetch_add(SetSharedRefCount(1), std::memory_order_relaxed);
		checkSlow(GetSharedRefCount(PreviousValue) < 0xffffff);
		if (GetSharedRefCount(PreviousValue) == 0)
		{
			AddWeakReference();
		}
	}

	inline void ReleaseSharedReference() const
	{
		const uint64 PreviousValue = ReferenceCountAndFlags.fetch_sub(SetSharedRefCount(1), std::memory_order_acq_rel);
		checkSlow(GetSharedRefCount(PreviousValue) > 0);
		if (GetSharedRefCount(PreviousValue) == 1)
		{
			const_cast<FSharedBuffer&>(*this).ReleaseData();
			ReleaseWeakReference();
		}
	}

	inline bool TryAddSharedReference() const
	{
		for (uint64 Value = ReferenceCountAndFlags.load(std::memory_order_relaxed);;)
		{
			if (GetSharedRefCount(Value) == 0)
			{
				return false;
			}
			if (ReferenceCountAndFlags.compare_exchange_weak(Value, Value + SetSharedRefCount(1),
				std::memory_order_relaxed, std::memory_order_relaxed))
			{
				return true;
			}
		}
	}

	inline void AddWeakReference() const
	{
		const uint64 PreviousValue = ReferenceCountAndFlags.fetch_add(SetWeakRefCount(1), std::memory_order_relaxed);
		checkSlow(GetWeakRefCount(PreviousValue) < 0xffffff);
	}

	inline void ReleaseWeakReference() const
	{
		const uint64 PreviousValue = ReferenceCountAndFlags.fetch_sub(SetWeakRefCount(1), std::memory_order_acq_rel);
		checkSlow(GetWeakRefCount(PreviousValue) > 0);
		if (GetWeakRefCount(PreviousValue) == 1)
		{
			DeleteBuffer(const_cast<FSharedBuffer*>(this));
		}
	}

	friend struct SharedBufferPrivate::FSharedRefOps;
	friend struct SharedBufferPrivate::FSharedPtrOps;
	friend struct SharedBufferPrivate::FWeakPtrOps;

private:
	void* Data;
	uint64 Size;
	mutable std::atomic<uint64> ReferenceCountAndFlags;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace SharedBufferPrivate
{

inline bool FSharedRefOps::TryAddRef(const FSharedBuffer* const Buffer)
{
	check(Buffer);
	verify(Buffer->TryAddSharedReference());
	return true;
}

inline void FSharedRefOps::AddRef(const FSharedBuffer* const Buffer)
{
	check(Buffer);
	Buffer->AddSharedReference();
}

inline void FSharedRefOps::Release(const FSharedBuffer* const Buffer)
{
	check(Buffer);
	Buffer->ReleaseSharedReference();
}

inline bool FSharedPtrOps::TryAddRef(const FSharedBuffer* const Buffer)
{
	return Buffer && Buffer->TryAddSharedReference();
}

inline void FSharedPtrOps::AddRef(const FSharedBuffer* const Buffer)
{
	if (Buffer)
	{
		Buffer->AddSharedReference();
	}
}

inline void FSharedPtrOps::Release(const FSharedBuffer* const Buffer)
{
	if (Buffer)
	{
		Buffer->ReleaseSharedReference();
	}
}

inline bool FWeakPtrOps::TryAddRef(const FSharedBuffer* const Buffer)
{
	if (Buffer)
	{
		Buffer->AddWeakReference();
		return true;
	}
	return false;
}

inline void FWeakPtrOps::AddRef(const FSharedBuffer* const Buffer)
{
	if (Buffer)
	{
		Buffer->AddWeakReference();
	}
}

inline void FWeakPtrOps::Release(const FSharedBuffer* const Buffer)
{
	if (Buffer)
	{
		Buffer->ReleaseWeakReference();
	}
}

}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
