// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "HAL/UnrealMemory.h"
#include "Memory/MemoryView.h"
#include "Misc/EnumClassFlags.h"
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
	Immutable = 1 << 1,
	HasDeleter = 1 << 2,
};

ENUM_CLASS_FLAGS(ESharedBufferFlags);

struct FSharedRefOps
{
	static bool TryAddRef(const FSharedBuffer* Buffer);
	static void AddRef(const FSharedBuffer* Buffer);
	static void Release(const FSharedBuffer* Buffer);
};

struct FSharedPtrOps
{
	static bool TryAddRef(const FSharedBuffer* Buffer);
	static void AddRef(const FSharedBuffer* Buffer);
	static void Release(const FSharedBuffer* Buffer);
};

struct FWeakPtrOps
{
	static bool TryAddRef(const FSharedBuffer* Buffer);
	static void AddRef(const FSharedBuffer* Buffer);
	static void Release(const FSharedBuffer* Buffer);
};

template <bool bInAllowNull, bool bInIsConst, bool bInIsWeak>
class TSharedBufferPtr
{
	static constexpr bool bAllowNull = bInAllowNull;
	static constexpr bool bIsConst = bInIsConst;
	static constexpr bool bIsWeak = bInIsWeak;
	using BufferType = std::conditional_t<bIsConst, const FSharedBuffer, FSharedBuffer>;
	using Ops = std::conditional_t<bIsWeak, FWeakPtrOps, std::conditional_t<bAllowNull, FSharedPtrOps, FSharedRefOps>>;

	template <bool bOtherAllowNull, bool bOtherIsConst, bool bOtherIsWeak>
	friend class TSharedBufferPtr;

	template <bool bOtherAllowNull, bool bOtherIsConst, bool bOtherIsWeak>
	static inline BufferType* CopyFrom(const TSharedBufferPtr<bOtherAllowNull, bOtherIsConst, bOtherIsWeak>& Ref)
	{
		BufferType* NewBuffer = Ref.Buffer;
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
	static inline BufferType* MoveFrom(TSharedBufferPtr<bOtherAllowNull, bOtherIsConst, bOtherIsWeak>&& Ref)
	{
		BufferType* NewBuffer = Ref.Buffer;
		if (bOtherIsWeak)
		{
			if (bIsWeak)
			{
				Ref.Buffer = nullptr;
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
					Ref.Buffer = nullptr;
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

	inline TSharedBufferPtr(const TSharedBufferPtr& Ref)
		: Buffer(CopyFrom(Ref))
	{
	}

	inline TSharedBufferPtr(TSharedBufferPtr&& Ref)
		: Buffer(MoveFrom(MoveTemp(Ref)))
	{
	}

	template <bool bOtherAllowNull, bool bOtherIsConst, bool bOtherIsWeak>
	inline explicit TSharedBufferPtr(const TSharedBufferPtr<bOtherAllowNull, bOtherIsConst, bOtherIsWeak>& Ref)
		: Buffer(CopyFrom(Ref))
	{
	}

	template <bool bOtherAllowNull, bool bOtherIsConst, bool bOtherIsWeak>
	inline explicit TSharedBufferPtr(TSharedBufferPtr<bOtherAllowNull, bOtherIsConst, bOtherIsWeak>&& Ref)
		: Buffer(MoveFrom(MoveTemp(Ref)))
	{
	}

	inline ~TSharedBufferPtr()
	{
		Ops::Release(Buffer);
	}

	inline TSharedBufferPtr& operator=(const TSharedBufferPtr& Ref)
	{
		BufferType* const OldBuffer = Buffer;
		Buffer = CopyFrom(Ref);
		Ops::Release(OldBuffer);
		return *this;
	}

	inline TSharedBufferPtr& operator=(TSharedBufferPtr&& Ref)
	{
		BufferType* const OldBuffer = Buffer;
		Buffer = MoveFrom(MoveTemp(Ref));
		Ops::Release(OldBuffer);
		return *this;
	}

	template <bool bOtherAllowNull, bool bOtherIsConst, bool bOtherIsWeak>
	inline TSharedBufferPtr& operator=(const TSharedBufferPtr<bOtherAllowNull, bOtherIsConst, bOtherIsWeak>& Ref)
	{
		BufferType* const OldBuffer = Buffer;
		Buffer = CopyFrom(Ref);
		Ops::Release(OldBuffer);
		return *this;
	}

	template <bool bOtherAllowNull, bool bOtherIsConst, bool bOtherIsWeak>
	inline TSharedBufferPtr& operator=(TSharedBufferPtr<bOtherAllowNull, bOtherIsConst, bOtherIsWeak>&& Ref)
	{
		BufferType* const OldBuffer = Buffer;
		Buffer = MoveFrom(MoveTemp(Ref));
		Ops::Release(OldBuffer);
		return *this;
	}

	BufferType* Get() const { return Buffer; }

private:
	BufferType* Buffer = nullptr;
};

}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** A non-nullable, thread-safe, reference to a mutable shared buffer. */
class FSharedBufferRef
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

	friend class FSharedBufferConstRef;
	friend class FSharedBufferPtr;
	friend class FSharedBufferConstPtr;
	friend class FSharedBufferWeakPtr;
	friend class FSharedBufferConstWeakPtr;

	PtrType Ptr;
};

/** A non-nullable, thread-safe, reference to a const shared buffer. */
class FSharedBufferConstRef
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

	friend class FSharedBufferConstPtr;
	friend class FSharedBufferConstWeakPtr;

	PtrType Ptr;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** A non-nullable, thread-safe, pointer to a mutable shared buffer. */
class FSharedBufferPtr
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

	friend class FSharedBufferConstPtr;
	friend class FSharedBufferWeakPtr;
	friend class FSharedBufferConstWeakPtr;

	PtrType Ptr;
};

/** A non-nullable, thread-safe, pointer to a const shared buffer. */
class FSharedBufferConstPtr
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

	friend class FSharedBufferConstWeakPtr;

	PtrType Ptr;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** A non-nullable, thread-safe, weak, pointer to a mutable shared buffer. */
class FSharedBufferWeakPtr
{
public:
	using PtrType = SharedBufferPrivate::TSharedBufferPtr</*bAllowNull*/ true, /*bIsConst*/ false, /*bIsWeak*/ true>;

	inline explicit FSharedBufferWeakPtr(PtrType&& InPtr) : Ptr(MoveTemp(InPtr)) {}

	inline FSharedBufferWeakPtr() : Ptr(nullptr) {}
	inline FSharedBufferWeakPtr(const FSharedBufferRef& Ref) : Ptr(Ref.Ptr) {}
	inline FSharedBufferWeakPtr(const FSharedBufferPtr& InPtr) : Ptr(InPtr.Ptr) {}

	inline FSharedBufferPtr Pin() { return FSharedBufferPtr(FSharedBufferPtr::PtrType(Ptr)); }

	inline void Reset() { *this = FSharedBufferWeakPtr(); }

	inline friend uint32 GetTypeHash(const FSharedBufferWeakPtr& InPtr) { return PointerHash(InPtr.Ptr.Get()); }

private:
	friend class FSharedBufferConstWeakPtr;

	PtrType Ptr;
};

/** A non-nullable, thread-safe, weak, pointer to a const shared buffer. */
class FSharedBufferConstWeakPtr
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

	inline FSharedBufferConstPtr Pin() { return FSharedBufferConstPtr(FSharedBufferConstPtr::PtrType(Ptr)); }

	inline void Reset() { *this = FSharedBufferConstWeakPtr(); }

	inline friend uint32 GetTypeHash(const FSharedBufferConstWeakPtr& InPtr) { return PointerHash(InPtr.Ptr.Get()); }

private:
	PtrType Ptr;
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
 *
 * Make an owned buffer by taking ownership of the buffer pointed to by the arguments.
 *
 *     FSharedBuffer::TakeOwnership(Data, Size, FMemory::Free)
 *     FSharedBuffer::TakeOwnership(Data, Size, [](void* Data) { delete[] static_cast<uint8*>(Data); })
 */
class FSharedBuffer
{
	using ESharedBufferFlags = SharedBufferPrivate::ESharedBufferFlags;

public:
	/** Make an owned writable buffer of Size bytes. */
	static inline FSharedBufferRef Alloc(const uint64 Size, const uint32 Alignment = DEFAULT_ALIGNMENT)
	{
		return NewBuffer(FMemory::Malloc(Size, Alignment), Size, ESharedBufferFlags::Owned);
	}

	/** Make an owned writable clone of the memory view. */
	static inline FSharedBufferRef Clone(const FConstMemoryView View)
	{
		return Clone(View.GetData(), View.GetSize());
	}

	/** Make an owned writable clone of the buffer. */
	static inline FSharedBufferRef Clone(const void* const Data, const uint64 Size)
	{
		const FSharedBufferRef Buffer = Alloc(Size);
		FMemory::Memcpy(Buffer->GetData(), Data, Size);
		return Buffer;
	}

	/** Make a non-owned writable view of the memory view. */
	static inline FSharedBufferRef MakeView(FMutableMemoryView View)
	{
		return MakeView(View.GetData(), View.GetSize());
	}

	/** Make a non-owned read-only view of the memory view. */
	static inline FSharedBufferConstRef MakeView(FConstMemoryView View)
	{
		return MakeView(View.GetData(), View.GetSize());
	}

	/** Make a non-owned writable view of the buffer. */
	static inline FSharedBufferRef MakeView(void* Data, uint64 Size)
	{
		return NewBuffer(Data, Size, ESharedBufferFlags::None);
	}

	/** Make a non-owned read-only view of the buffer. */
	static inline FSharedBufferConstRef MakeView(const void* Data, uint64 Size)
	{
		// const_cast is safe here because the return value only allows accessing data through a const void*
		return MakeView(const_cast<void*>(Data), Size);
	}

	/**
	 * Make an owned writable buffer by taking ownership of the provided memory.
	 *
	 * @param Deleter Called with the data pointer to free memory when there are no shared references.
	 */
	template <typename DeleterType>
	static inline FSharedBufferRef TakeOwnership(void* Data, uint64 Size, DeleterType&& Deleter)
	{
		return NewBufferWithDeleter(Data, Size, ESharedBufferFlags::Owned, Forward<DeleterType>(Deleter));
	}

	/**
	 * Make an owned read-only buffer by taking ownership of the provided memory.
	 *
	 * @param Deleter Called with the data pointer to free memory when there are no shared references.
	 */
	template <typename DeleterType>
	static inline FSharedBufferConstRef TakeOwnership(const void* Data, uint64 Size, DeleterType&& Deleter)
	{
		// const_cast is safe here because the return value only allows accessing data through a const void*
		return TakeOwnership(const_cast<void*>(Data), Size, Forward<DeleterType>(Deleter));
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
	static inline auto MakeOwned(T&& Buffer) -> decltype(std::decay_t<T>(Clone(Buffer->GetData(), Buffer->GetSize())))
	{
		return !IsValid(Buffer) || Buffer->IsOwned() ? Forward<T>(Buffer) : Clone(Buffer->GetData(), Buffer->GetSize());
	}

	/** Return the buffer if it is owned and either immutable or this is the only reference, or a clone otherwise. */
	static inline FSharedBufferConstRef MakeImmutable(FSharedBufferRef&& Buffer)
	{
		const FSharedBuffer& BufferRef = *Buffer;
		return BufferRef.TryMakeImmutable() ? MoveTemp(Buffer) : MakeImmutable(Clone(*Buffer));
	}

	/** Return the buffer if it is owned and either immutable or this is the only reference, or a clone otherwise. */
	static inline FSharedBufferConstRef MakeImmutable(FSharedBufferConstRef&& Buffer)
	{
		return Buffer->TryMakeImmutable() ? MoveTemp(Buffer) : MakeImmutable(Clone(*Buffer));
	}

	/** Return the buffer if it is owned and either immutable or this is the only reference, or a clone otherwise. */
	static inline FSharedBufferConstPtr MakeImmutable(FSharedBufferPtr&& Buffer)
	{
		return MakeImmutable(FSharedBufferConstPtr(MoveTemp(Buffer)));
	}

	/** Return the buffer if it is owned and either immutable or this is the only reference, or a clone otherwise. */
	static inline FSharedBufferConstPtr MakeImmutable(FSharedBufferConstPtr&& Buffer)
	{
		return Buffer ? Buffer->TryMakeImmutable() ? MoveTemp(Buffer) : MakeImmutable(Clone(*Buffer)) : FSharedBufferConstPtr();
	}

	// Disable overloads that would allow the argument to be copied.
	static void MakeImmutable(const FSharedBufferRef&) = delete;
	static void MakeImmutable(const FSharedBufferConstRef&) = delete;
	static void MakeImmutable(const FSharedBufferPtr&) = delete;
	static void MakeImmutable(const FSharedBufferConstPtr&) = delete;

	/** Return the buffer because it is already mutable. */
	static inline FSharedBufferRef MakeMutable(FSharedBufferRef&& Buffer) { return MoveTemp(Buffer); }

	/** Return the buffer if it is owned and either mutable or this is the only reference, or a clone otherwise. */
	static inline FSharedBufferRef MakeMutable(FSharedBufferConstRef&& Buffer)
	{
		if (Buffer->TryMakeMutable())
		{
			return FSharedBufferRef(FSharedBufferRef::PtrType(const_cast<FSharedBuffer*>(&Buffer.Get())));
		}
		return Clone(*Buffer);
	}

	/** Return the buffer because it is already mutable. */
	static inline FSharedBufferPtr MakeMutable(FSharedBufferPtr&& Buffer) { return MoveTemp(Buffer); }

	/** Return the buffer if it is owned and either mutable or this is the only reference, or a clone otherwise. */
	static inline FSharedBufferPtr MakeMutable(FSharedBufferConstPtr&& Buffer)
	{
		if (Buffer->TryMakeMutable())
		{
			return FSharedBufferPtr(FSharedBufferPtr::PtrType(const_cast<FSharedBuffer*>(Buffer.Get())));
		}
		return Clone(*Buffer);
	}

	// Disable overloads that would allow the argument to be copied.
	static void MakeMutable(const FSharedBufferRef&) = delete;
	static void MakeMutable(const FSharedBufferConstRef&) = delete;
	static void MakeMutable(const FSharedBufferPtr&) = delete;
	static void MakeMutable(const FSharedBufferConstPtr&) = delete;

	/** A pointer to the start of the buffer. */
	inline void* GetData() { return Data; }
	/** A pointer to the start of the buffer. */
	inline const void* GetData() const { return Data; }

	/** The size of the buffer in bytes. */
	inline uint64 GetSize() const { return Size; }

	/** Whether the shared buffer owns the memory that it provides a view of. */
	inline bool IsOwned() const { return (GetFlags(ReferenceCountAndFlags) & ESharedBufferFlags::Owned) != 0; }

	/**
	 * Whether the shared buffer is immutable.
	 *
	 * An immutable shared buffer is owned and every reference or pointer to it is const.
	 */
	inline bool IsImmutable() const { return (GetFlags(ReferenceCountAndFlags) & ESharedBufferFlags::Immutable) != 0; }

	/**
	 * Try to make the shared buffer immutable.
	 *
	 * A shared buffer can be made immutable if it is owned and there is only one reference to it.
	 *
	 * @return true if the buffer was immutable or was made immutable, otherwise false.
	 */
	CORE_API bool TryMakeImmutable() const;

	// Disable this overload because it would allow an immutable buffer to be mutated.
	bool TryMakeImmutable() = delete;

	/**
	 * Try to make the shared buffer mutable.
	 *
	 * A shared buffer can be made mutable if it is owned and there is only one reference to it.
	 *
	 * @return true if the buffer was mutable or was made mutable, otherwise false.
	 */
	CORE_API bool TryMakeMutable() const;

	/** The shared buffer is already mutable. */
	inline bool TryMakeMutable() { return true; }

	/** A mutable view of buffer. */
	inline FMutableMemoryView GetView() { return FMutableMemoryView(GetData(), GetSize()); }
	/** A const view of the buffer. */
	inline FConstMemoryView GetView() const { return FConstMemoryView(GetData(), GetSize()); }

	inline operator FMutableMemoryView() { return GetView(); }
	inline operator FConstMemoryView() const { return GetView(); }

private:
	/**
	 * Construct a new shared buffer with a custom deleter.
	 *
	 * @param Data A pointer to the start of the data buffer.
	 * @param Size The size of the data buffer.
	 * @param Flags The flags associated with the data buffer. Internally adds HasDeleter.
	 * @param Delete A callable to delete the data buffer when it is released.
	 */
	template <typename DeleterType>
	static inline FSharedBufferRef NewBufferWithDeleter(
		void* const Data,
		const uint64 Size,
		ESharedBufferFlags Flags,
		DeleterType&& Deleter)
	{
		Flags |= ESharedBufferFlags::HasDeleter;
		static_assert(alignof(TSharedBufferDeleter<DeleterType>) <= alignof(FSharedBuffer), "Required alignment is too high.");
		FSharedBufferRef Buffer = NewBuffer(Data, Size, Flags, sizeof(TSharedBufferDeleter<DeleterType>));
		new(&Buffer.Get() + 1) TSharedBufferDeleter<DeleterType>(Forward<DeleterType>(Deleter));
		return Buffer;
	}

	/**
	 * Construct a new shared buffer.
	 *
	 * @param Data A pointer to the start of the data buffer.
	 * @param Size The size of the data buffer.
	 * @param Flags The flags associated with the data buffer.
	 * @param DeleterSize The size of the optional deleter to allocate after the FSharedBuffer.
	 */
	CORE_API static FSharedBufferRef NewBuffer(void* Data, uint64 Size, ESharedBufferFlags Flags, uint32 DeleterSize = 0);

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

private:
	class FSharedBufferDeleter
	{
	public:
		/** Free the memory for the buffer and destroy the deleter. */
		virtual void Free(void* Data) = 0;

	protected:
		FSharedBufferDeleter() = default;
		~FSharedBufferDeleter() = default;
	};

	template <typename DeleterType>
	class TSharedBufferDeleter final : public FSharedBufferDeleter
	{
	public:
		explicit TSharedBufferDeleter(DeleterType&& InDeleter)
			: Deleter(Forward<DeleterType>(InDeleter))
		{
		}

	private:
		~TSharedBufferDeleter() = default;

		virtual void Free(void* const InData) final
		{
			Deleter(InData);
			this->~TSharedBufferDeleter();
		}

		std::decay_t<DeleterType> Deleter;
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
